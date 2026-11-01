# 霞鹜新晰黑字体与原版替换

界面采用 LXGW 的[霞鹜新晰黑](https://github.com/lxgw/LxgwNeoXiHei)，固定版本为 `v1.308`、来源提交为 `f786a5c5c51c2339542be296598e25d279ce79ef`。上游由 IPAex Gothic 修改而来，适用 [IPA Font License Agreement v1.0](IPA-Font-License-v1.0.txt)；字体归属为 LXGW 与 Information-technology Promotion Agency, Japan（IPA）。

`LXGWNeoXiHei.ttf` 的 SHA256 为 `80415d3666d9f51cf42ecf82c5350488e6b3f282ab46a3b90dcaa5e212564ecc`。构建仅提取界面与卡片需要的字形，将其转换为 24 / 32 / 48px 的 1bpp 字档及 176 / 112px 特大字位图；这些字体派生数据适用上述 IPA 许可。应用代码与原创 SVG 线稿的许可不由此变更。

字体获取及摘要校验位于 [fetch-ui-font.sh](../tools/fetch-ui-font.sh) 与 [versions.sh](../tools/versions.sh)，字形提取与转换位于 [build-native.ts](../tools/build-native.ts) 和 [sprites.ts](../tools/sprites.ts)。上述源码提供继续修改字体数据的方法。TTF、生成字档、位图及固件均保存在被 Git 忽略的 `.tools/` 与 `.build/`；分发这些派生数据时一并提供本说明、完整 IPA 许可和相关构建源码。

可以把所继承的 IPAex 原有字形替换为原版 IPAex Gothic 004.01。官方文件为 <https://moji.or.jp/wp-content/ipafont/IPAexfont/ipaexg00401.zip>；ZIP 的 SHA256 为 `4ab6fee94bf6f94b3eeb31be2b73c559f738dc6b336d722d5301b1f3f592f850`，其中 `ipaexg.ttf` 的 SHA256 为 `3b9955a5e437ffb24c41548e79296fa724822da21ee75dc1b94f6ccdb8f400dd`。下载入口保留原版文件名并核验这两个摘要。

在仓库根目录执行：

```sh
BALABALA_UI_FONT=ipaex CARGO_PROFILE_RELEASE_OPT_LEVEL=z sh tools/build.sh build
```

该模式以原版 IPAex Gothic 为首选；原版未包含的简体汉字等字符仍由新晰黑补齐，以维持当前内容和导入字形范围。刷机模式最终提示与旧宽度契约继续使用已有 Noto 字档。默认字体可通过下列命令恢复：

```sh
CARGO_PROFILE_RELEASE_OPT_LEVEL=z sh tools/build.sh build
```

生成产物见 `.build/firmware/balabala_microts_probe.{elf,map,bin}`。选择字体不会自动写入设备；烧录仍须使用与实际设备、分区及备份相符的流程。
