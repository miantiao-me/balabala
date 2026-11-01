#include "os/os.h"
#include "esp_nimble_mem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(MYNEWT_VAL(MP_RUNTIME_ALLOC) == 1, "runtime allocator required");
_Static_assert(MYNEWT_VAL(MP_BLOCK_REUSED) == 0, "returned blocks must be freed");
_Static_assert(MYNEWT_VAL(BLE_STATIC_TO_DYNAMIC) == 1, "dynamic pool registry required");

static size_t allocations, releases, attempts, live, expected_size;
static void *owned[24];
static bool fail_next, critical;

void mempool_test_enter(void) { assert(!critical); critical = true; }
void mempool_test_exit(void) { assert(critical); critical = false; }

void *nimble_mem_malloc(size_t size)
{
    assert(!critical && size == expected_size);
    attempts++;
    if (fail_next) {
        fail_next = false;
        return NULL;
    }
    void *p = malloc(size);
    assert(p);
    for (size_t i = 0; i < 24; i++) {
        if (!owned[i]) {
            owned[i] = p;
            live++;
            allocations++;
            return p;
        }
    }
    abort();
}

void nimble_mem_free(void *p)
{
    assert(!critical && p);
    for (size_t i = 0; i < 24; i++) {
        if (owned[i] == p) {
            owned[i] = NULL;
            live--;
            releases++;
            free(p);
            return;
        }
    }
    abort();
}

static void only_pool(struct os_mempool *pool)
{
    struct os_mempool_info info;
    assert(os_mempool_info_get_next(NULL, &info) == pool);
    if (pool) {
        assert(info.omi_num_free == pool->mp_num_free);
        assert(os_mempool_info_get_next(pool, &info) == NULL);
    }
}

static void exercise(struct os_mempool *pool, uint16_t count, uint32_t size)
{
    void *blocks[24];
    expected_size = OS_ALIGN(size, OS_ALIGNMENT);
    size_t before = attempts;
    assert(os_mempool_init(pool, count, size, NULL, "runtime-test") == OS_OK);
    assert(attempts == before && live == 0);
    assert(pool->mp_flags & OS_MEMPOOL_F_RUNTIME);
    assert(!(pool->mp_flags & OS_MEMPOOL_F_REUSED));
    only_pool(pool);

    fail_next = true;
    assert(os_memblock_get(pool) == NULL);
    assert(pool->mp_num_free == count && live == 0);
    for (uint16_t i = 0; i < count; i++) {
        if (i == count / 2) {
            fail_next = true;
            assert(os_memblock_get(pool) == NULL);
            assert(pool->mp_num_free == count - i && live == i);
        }
        blocks[i] = os_memblock_get(pool);
        assert(blocks[i] && live == (size_t)i + 1);
        memset(blocks[i], 0xa5, size);
        assert(pool->mp_num_free == count - i - 1);
    }
    before = attempts;
    assert(os_memblock_get(pool) == NULL && attempts == before);
    assert(pool->mp_min_free == 0);
    for (uint16_t i = count; i > 0; i--) {
        size_t freed = releases;
        assert(os_memblock_put(pool, blocks[i - 1]) == OS_OK);
        assert(releases == freed + 1 && live == i - 1);
    }
    assert(pool->mp_num_free == count && SLIST_EMPTY(pool));
    assert(allocations == releases);
    assert(os_mempool_clear(pool) == OS_OK);
    assert(pool->mp_min_free == count);
    // clear/reinit要求调用方先归还所有块，不把它当作释放在途块的接口。
    assert(os_mempool_init(pool, count, size, NULL, "runtime-test") == OS_OK);
    only_pool(pool);
    blocks[0] = os_memblock_get(pool);
    assert(blocks[0]);
    assert(os_memblock_put(pool, blocks[0]) == OS_OK);
    assert(os_mempool_unregister(pool) == OS_OK);
    only_pool(NULL);
    assert(live == 0 && allocations == releases);
}

static void registry_matches(struct os_mempool **expected, size_t count)
{
    struct os_mempool_info info;
    struct os_mempool *cur = NULL;
    for (size_t i = 0; i < count; i++) {
        cur = os_mempool_info_get_next(cur, &info);
        assert(cur == expected[i]);
    }
    assert(os_mempool_info_get_next(cur, &info) == NULL);
}

static void registry_reinit(void)
{
    static const char name_a[] = "registry-a", name_b[] = "registry-b", name_c[] = "registry-c";
    struct os_mempool a = {0}, b = {0}, c = {0}, replacement = {0};
    for (unsigned cycle = 0; cycle < 40; cycle++) {
        assert(os_mempool_init(&a, 2, 64, NULL, name_a) == OS_OK);
        assert(os_mempool_init(&a, 2, 64, NULL, name_a) == OS_OK);
        only_pool(&a);
        // 不同对象、同一name指针替换最后一个池，保持上游身份语义。
        assert(os_mempool_init(&replacement, 2, 64, NULL, name_a) == OS_OK);
        only_pool(&replacement);
        assert(os_mempool_unregister(&a) == OS_INVALID_PARM);
        assert(os_mempool_unregister(&replacement) == OS_OK);
        only_pool(NULL);
        assert(os_mempool_init(&a, 2, 64, NULL, name_a) == OS_OK);
        only_pool(&a);
        assert(os_mempool_init(&b, 2, 64, NULL, name_b) == OS_OK);
        assert(os_mempool_init(&c, 2, 64, NULL, name_c) == OS_OK);
        expected_size = 64;
        void *held = os_memblock_get(&a);
        assert(held);
        assert(os_mempool_init(&b, 2, 64, NULL, name_b) == OS_OK);
        struct os_mempool *same[] = {&a, &c, &b};
        registry_matches(same, 3);
        assert(os_mempool_init(&replacement, 2, 64, NULL, name_c) == OS_OK);
        struct os_mempool *replaced[] = {&a, &b, &replacement};
        registry_matches(replaced, 3);
        assert(live == 1 && a.mp_num_free == 1);
        assert(os_memblock_put(&a, held) == OS_OK);
        os_mempool_deinit(false);
        only_pool(NULL);
        assert(live == 0 && allocations == releases);
    }
    // 静态分支只验证注册表；宿主64位地址不用于C3的32位静态块寻址。
    _Alignas(4) unsigned char storage[64];
    assert(os_mempool_init(&a, 1, sizeof(storage), storage, name_a) == OS_OK);
    assert(os_mempool_init(&a, 1, sizeof(storage), storage, name_a) == OS_OK);
    only_pool(&a);
    assert(os_mempool_unregister(&a) == OS_OK);
    only_pool(NULL);
}

static os_error_t return_from_callback(struct os_mempool_ext *pool, void *block, void *arg)
{
    (*(int *)arg)++;
    return os_memblock_put_from_cb(&pool->mpe_mp, block);
}

int main(void)
{
    struct os_mempool pool = {0};
    for (unsigned cycle = 0; cycle < 40; cycle++) {
        os_mempool_module_init();
        exercise(&pool, 24, 300); // C3 ACL整池7200B的逐块申请尺寸。
        exercise(&pool, 12, 256);
        exercise(&pool, 24, 320);
        os_mempool_deinit(false);
        only_pool(NULL);
    }
    struct os_mempool_ext ext = {0};
    int callbacks = 0;
    expected_size = 300;
    assert(os_mempool_ext_init(&ext, 24, 300, NULL, "acl-ext") == OS_OK);
    ext.mpe_put_cb = return_from_callback;
    ext.mpe_put_arg = &callbacks;
    void *block = os_memblock_get(&ext.mpe_mp);
    assert(block && os_memblock_put(&ext.mpe_mp, block) == OS_OK);
    assert(callbacks == 1 && live == 0);
    assert(os_mempool_ext_clear(&ext) == OS_OK);
    assert(ext.mpe_put_cb == NULL && (ext.mpe_mp.mp_flags & OS_MEMPOOL_F_RUNTIME));
    os_mempool_deinit(false);
    only_pool(NULL);
    registry_reinit();
    assert(allocations == releases && !critical);
    printf("real IDF NimBLE mempool: runtime/nonreuse failure restores capacity, exhaustion, immediate free, clear/reinit, ext callback, 40 ACL/MSYS cycles + 40 last/multiple/replaced registry cycles without residual blocks passed (%zu allocations/%zu frees)\n", allocations, releases);
    return 0;
}
