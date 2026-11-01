<script setup lang="ts">
import { View, Text } from "@pocketjs/framework/vue-vapor/components";
import type { u8 } from "@pocketjs/framework/vue-vapor/std";
const props = defineProps<{
  title: string; hero: string;
  line0: string; line1: string; line2: string;
  line3: string; line4: string; line5: string;
  hint: string; keys: string; success: boolean; download: boolean; provisioning: boolean;
  screen: u8; questionSize: u8; optionSize: u8;
  opt0: string; opt1: string; opt2: string; opt3: string;
}>();
</script>

<template>
  <View class="w-[480] h-[800] bg-white">
    <View v-if="props.download" class="absolute left-[0] top-[-8] w-[480] h-[800] flex-col items-center justify-center gap-[24]">
      <Text class="text-5xl text-black">{{ props.hero }}</Text>
      <Text class="text-2xl text-black">{{ props.line0 }}</Text>
    </View>
    <View v-else-if="props.provisioning" class="absolute left-[0] top-[0] w-[480] h-[800]">
      <Text class="absolute left-[24] top-[24] text-5xl font-bold text-black">蓝牙配网</Text>
      <Text class="absolute left-[24] top-[106] text-2xl font-bold text-black">设备名称</Text>
      <Text class="absolute left-[24] top-[226] text-2xl font-bold text-black">配对密码</Text>
      <Text class="absolute left-[24] top-[372] text-2xl font-bold text-black">配网进度</Text>
      <Text class="absolute left-[24] top-[628] text-2xl font-bold text-black">反白表示当前状态</Text>
      <Text class="absolute left-[24] top-[666] text-2xl font-bold text-black">手机打开乐鑫配网客户端</Text>
      <Text class="absolute left-[24] top-[704] text-2xl font-bold text-black">返回或确认退出</Text>
      <Text class="absolute left-[24] top-[142] text-5xl font-bold text-black">{{ props.line0 }}</Text>
      <Text class="absolute left-[312] top-[40] text-2xl font-bold text-black">{{ props.hint }}</Text>
    </View>
    <View v-else-if="props.screen === $screen.home" class="absolute left-[0] top-[0] w-[480] h-[800]">
      <Text class="absolute left-[$head.status.x] top-[$head.status.y] w-[$head.status.w] text-2xl font-bold text-right text-black">{{ props.hint }}</Text>
      <Text class="absolute left-[$home.hero.x] top-[$home.hero.y] text-5xl font-bold text-black">{{ props.hero }}</Text>
      <View class="absolute left-[$home.chip.x] top-[$home.chip.y] w-[$home.chip.w] h-[$home.chip.h] flex-col items-center justify-center">
        <Text class="text-2xl font-bold text-black">{{ props.title }}</Text>
      </View>
      <Text class="absolute left-[$home.tiles.0.x] top-[$home.tileLabels.0] w-[$home.tiles.0.w] text-2xl font-bold text-center text-black">汉字</Text>
      <Text class="absolute left-[$home.tiles.1.x] top-[$home.tileLabels.1] w-[$home.tiles.1.w] text-2xl font-bold text-center text-black">拼音</Text>
      <Text class="absolute left-[$home.tiles.2.x] top-[$home.tileLabels.2] w-[$home.tiles.2.w] text-2xl font-bold text-center text-black">英语</Text>
      <Text class="absolute left-[$home.tiles.3.x] top-[$home.tileLabels.3] w-[$home.tiles.3.w] text-2xl font-bold text-center text-black">数学</Text>
    </View>
    <View v-else-if="props.screen === $screen.deck" class="absolute left-[0] top-[0] w-[480] h-[800]">
      <View class="absolute left-[$head.chip.x] top-[$head.chip.y] w-[$head.chip.w] h-[$head.chip.h] flex-col items-center justify-center">
        <Text class="text-2xl font-bold text-black">{{ props.title }}</Text>
      </View>
      <Text class="absolute left-[$head.title.x] top-[$head.title.y] text-2xl font-bold text-black">{{ props.hero }}</Text>
      <Text class="absolute left-[$head.status.x] top-[$head.status.y] w-[$head.status.w] text-2xl font-bold text-right text-black">{{ props.hint }}</Text>
      <Text class="absolute left-[$deck.names.0.x] top-[$deck.names.0.y] text-5xl font-bold text-black">认一认</Text>
      <Text class="absolute left-[$deck.names.1.x] top-[$deck.names.1.y] text-5xl font-bold text-black">选一选</Text>
      <Text class="absolute left-[$deck.names.2.x] top-[$deck.names.2.y] text-5xl font-bold text-black">再看看</Text>
      <Text class="absolute left-[$deck.notes.0.x] top-[$deck.notes.0.y] text-2xl font-bold text-black">看卡片，翻一翻</Text>
      <Text class="absolute left-[$deck.notes.1.x] top-[$deck.notes.1.y] text-2xl font-bold text-black">四个里面选一个</Text>
      <Text class="absolute left-[$deck.notes.2.x] top-[$deck.notes.2.y] text-2xl font-bold text-black">{{ props.line0 }}</Text>
    </View>
    <View v-else-if="props.screen === $screen.read" class="absolute left-[0] top-[0] w-[480] h-[800]">
      <View class="absolute left-[$head.chip.x] top-[$head.chip.y] w-[$head.chip.w] h-[$head.chip.h] flex-col items-center justify-center">
        <Text class="text-2xl font-bold text-black">{{ props.title }}</Text>
      </View>
      <Text class="absolute left-[$head.status.x] top-[$head.status.y] w-[$head.status.w] text-2xl font-bold text-right text-black">{{ props.hint }}</Text>
      <View class="absolute left-[$read.front.x] top-[$read.front.y] w-[$read.front.w] h-[$read.front.h] flex-col items-center justify-center">
        <Text class="w-[$read.front.w] text-5xl font-bold leading-[$read.leading.0] text-center text-black">{{ props.hero }}</Text>
      </View>
      <View class="absolute left-[$read.backFront.x] top-[$read.backFront.y] w-[$read.backFront.w] h-[$read.backFront.h] flex-col items-center justify-center">
        <Text :class="props.optionSize === 15 ? 'w-[$read.backFront.w] text-5xl font-bold leading-[$read.leading.0] text-center text-black' : 'w-[$read.backFront.w] text-2xl font-bold leading-[$read.leading.1] text-center text-black'">{{ props.line0 }}</Text>
      </View>
      <View class="absolute left-[$read.back.x] top-[$read.back.y] w-[$read.back.w] h-[$read.back.h] flex-col items-center justify-center">
        <Text :class="props.questionSize === 15 ? 'w-[$read.back.w] text-5xl font-bold leading-[$read.leading.0] text-center text-black' : 'w-[$read.back.w] text-2xl font-bold leading-[$read.leading.1] text-center text-black'">{{ props.line1 }}</Text>
      </View>
    </View>
    <View v-else-if="props.screen === $screen.quiz" class="absolute left-[0] top-[0] w-[480] h-[800]">
      <View class="absolute left-[$head.chip.x] top-[$head.chip.y] w-[$head.chip.w] h-[$head.chip.h] flex-col items-center justify-center">
        <Text class="text-2xl font-bold text-black">{{ props.title }}</Text>
      </View>
      <Text class="absolute left-[$head.status.x] top-[$head.status.y] w-[$head.status.w] text-2xl font-bold text-right text-black">{{ props.hint }}</Text>
      <View class="absolute left-[$quiz.question.x] top-[$quiz.question.y] w-[$quiz.question.w] h-[$quiz.question.h] flex-col items-center justify-center">
        <Text :class="props.questionSize === 15 ? 'w-[$quiz.question.w] text-5xl font-bold leading-[$quiz.questionLeading.0] text-center text-black' : 'w-[$quiz.question.w] text-2xl font-bold leading-[$quiz.questionLeading.1] text-center text-black'">{{ props.hero }}</Text>
      </View>
      <Text class="absolute left-[$quiz.feedbackTitle.x] top-[$quiz.feedbackTitle.y] w-[$quiz.feedbackTitle.w] text-5xl font-bold text-center text-black">{{ props.line0 }}</Text>
      <Text class="absolute left-[$quiz.feedbackBody.x] top-[$quiz.feedbackBody.y] w-[$quiz.feedbackBody.w] text-2xl font-bold leading-[$quiz.questionLeading.1] text-center text-black">{{ props.line1 }}</Text>
      <View class="absolute left-[$quiz.options.0.x] top-[$quiz.options.0.y] w-[$quiz.options.0.w] h-[$quiz.options.0.h] flex-col items-center justify-center">
        <Text :class="props.optionSize === 15 ? 'w-[$quiz.options.0.w] text-5xl font-bold leading-[$quiz.optionLeading.0] text-center text-black' : props.optionSize === 6 ? 'w-[$quiz.options.0.w] text-4xl leading-[$quiz.optionLeading.1] text-center text-black' : 'w-[$quiz.options.0.w] text-2xl font-bold leading-[$quiz.optionLeading.2] text-center text-black'">{{ props.opt0 }}</Text>
      </View>
      <View class="absolute left-[$quiz.options.1.x] top-[$quiz.options.1.y] w-[$quiz.options.1.w] h-[$quiz.options.1.h] flex-col items-center justify-center">
        <Text :class="props.optionSize === 15 ? 'w-[$quiz.options.0.w] text-5xl font-bold leading-[$quiz.optionLeading.0] text-center text-black' : props.optionSize === 6 ? 'w-[$quiz.options.0.w] text-4xl leading-[$quiz.optionLeading.1] text-center text-black' : 'w-[$quiz.options.0.w] text-2xl font-bold leading-[$quiz.optionLeading.2] text-center text-black'">{{ props.opt1 }}</Text>
      </View>
      <View class="absolute left-[$quiz.options.2.x] top-[$quiz.options.2.y] w-[$quiz.options.2.w] h-[$quiz.options.2.h] flex-col items-center justify-center">
        <Text :class="props.optionSize === 15 ? 'w-[$quiz.options.0.w] text-5xl font-bold leading-[$quiz.optionLeading.0] text-center text-black' : props.optionSize === 6 ? 'w-[$quiz.options.0.w] text-4xl leading-[$quiz.optionLeading.1] text-center text-black' : 'w-[$quiz.options.0.w] text-2xl font-bold leading-[$quiz.optionLeading.2] text-center text-black'">{{ props.opt2 }}</Text>
      </View>
      <View class="absolute left-[$quiz.options.3.x] top-[$quiz.options.3.y] w-[$quiz.options.3.w] h-[$quiz.options.3.h] flex-col items-center justify-center">
        <Text :class="props.optionSize === 15 ? 'w-[$quiz.options.0.w] text-5xl font-bold leading-[$quiz.optionLeading.0] text-center text-black' : props.optionSize === 6 ? 'w-[$quiz.options.0.w] text-4xl leading-[$quiz.optionLeading.1] text-center text-black' : 'w-[$quiz.options.0.w] text-2xl font-bold leading-[$quiz.optionLeading.2] text-center text-black'">{{ props.opt3 }}</Text>
      </View>
    </View>
    <View v-else-if="props.screen === $screen.settings" class="absolute left-[0] top-[0] w-[480] h-[800]">
      <Text class="absolute left-[$settings.title.x] top-[$settings.title.y] text-5xl font-bold text-black">{{ props.hero }}</Text>
      <Text class="absolute left-[$head.status.x] top-[$head.status.y] w-[$head.status.w] text-2xl font-bold text-right text-black">{{ props.hint }}</Text>
      <Text class="absolute left-[$settings.labels.0.x] top-[$settings.labels.0.y] w-[$settings.labels.0.w] text-2xl font-bold text-black">{{ props.opt0 }}</Text>
      <Text class="absolute left-[$settings.labels.1.x] top-[$settings.labels.1.y] w-[$settings.labels.1.w] text-2xl font-bold text-black">{{ props.opt1 }}</Text>
      <Text class="absolute left-[$settings.labels.2.x] top-[$settings.labels.2.y] w-[$settings.labels.2.w] text-2xl font-bold text-black">{{ props.opt2 }}</Text>
      <Text class="absolute left-[$settings.labels.3.x] top-[$settings.labels.3.y] w-[$settings.labels.3.w] text-2xl font-bold text-black">{{ props.opt3 }}</Text>
      <Text class="absolute left-[$settings.labels.4.x] top-[$settings.labels.4.y] w-[$settings.labels.4.w] text-2xl font-bold text-black">{{ props.line4 }}</Text>
      <Text class="absolute left-[$settings.value.x] top-[$settings.value.y] w-[$settings.value.w] text-2xl font-bold text-right text-black">{{ props.line0 }}</Text>
      <Text class="absolute left-[$settings.note.x] top-[$settings.note.y] w-[$settings.note.w] text-2xl font-bold leading-[$message.bodyLeading] text-black">{{ props.line5 }}</Text>
    </View>
    <View v-else-if="props.screen === $screen.stats" class="absolute left-[0] top-[0] w-[480] h-[800]">
      <Text class="absolute left-[$stats.title.x] top-[$stats.title.y] text-5xl font-bold text-black">学习情况</Text>
      <Text class="absolute left-[$head.status.x] top-[$head.status.y] w-[$head.status.w] text-2xl font-bold text-right text-black">{{ props.hint }}</Text>
      <Text class="absolute left-[$stats.counts.0.x] top-[$stats.counts.0.y] w-[$stats.counts.0.w] text-2xl font-bold text-right text-black">{{ props.line0 }}</Text>
      <Text class="absolute left-[$stats.counts.1.x] top-[$stats.counts.1.y] w-[$stats.counts.1.w] text-2xl font-bold text-right text-black">{{ props.line1 }}</Text>
      <Text class="absolute left-[$stats.counts.2.x] top-[$stats.counts.2.y] w-[$stats.counts.2.w] text-2xl font-bold text-right text-black">{{ props.line2 }}</Text>
      <Text class="absolute left-[$stats.counts.3.x] top-[$stats.counts.3.y] w-[$stats.counts.3.w] text-2xl font-bold text-right text-black">{{ props.line3 }}</Text>
    </View>
    <View v-else-if="props.screen === $screen.sleep" class="absolute left-[0] top-[0] w-[480] h-[800]">
      <Text class="absolute left-[$sleep.title.x] top-[$sleep.title.y] w-[$sleep.title.w] text-5xl font-bold text-center text-black">{{ props.hero }}</Text>
      <Text class="absolute left-[$sleep.body.x] top-[$sleep.body.y] w-[$sleep.body.w] text-2xl font-bold leading-[$message.bodyLeading] text-center text-black">{{ props.line0 }}</Text>
    </View>
    <View v-else-if="props.screen === $screen.message" class="absolute left-[0] top-[0] w-[480] h-[800]">
      <Text class="absolute left-[$head.status.x] top-[$head.status.y] w-[$head.status.w] text-2xl font-bold text-right text-black">{{ props.hint }}</Text>
      <Text class="absolute left-[$message.title.x] top-[$message.title.y] w-[$message.title.w] text-5xl font-bold text-center text-black">{{ props.hero }}</Text>
      <Text class="absolute left-[$message.body.x] top-[$message.body.y] w-[$message.body.w] text-2xl font-bold leading-[$message.bodyLeading] text-center text-black">{{ props.line0 }}</Text>
      <View class="absolute left-[$message.buttons.0.x] top-[$message.buttons.0.y] w-[$message.buttons.0.w] h-[$message.buttons.0.h] flex-col items-center justify-center">
        <Text class="text-5xl font-bold text-black">{{ props.opt0 }}</Text>
      </View>
      <View class="absolute left-[$message.buttons.1.x] top-[$message.buttons.1.y] w-[$message.buttons.1.w] h-[$message.buttons.1.h] flex-col items-center justify-center">
        <Text class="text-5xl font-bold text-black">{{ props.opt1 }}</Text>
      </View>
    </View>
    <View v-else class="absolute left-[0] top-[0] w-[480] h-[800]">
    <Text class="absolute left-[28] top-[24] text-2xl text-black">{{ props.title }}</Text>
    <View class="absolute left-[28] top-[76] w-[424] h-[3] bg-black" />
    <Text class="absolute left-[28] top-[104] text-5xl text-black">{{ props.hero }}</Text>
    <Text class="absolute left-[28] top-[198] text-2xl text-black">{{ props.line0 }}</Text>
    <Text class="absolute left-[28] top-[268] text-2xl text-black">{{ props.line1 }}</Text>
    <Text class="absolute left-[28] top-[338] text-2xl text-black">{{ props.line2 }}</Text>
    <Text class="absolute left-[28] top-[408] text-2xl text-black">{{ props.line3 }}</Text>
    <Text class="absolute left-[28] top-[478] text-2xl text-black">{{ props.line4 }}</Text>
    <Text class="absolute left-[28] top-[548] text-2xl text-black">{{ props.line5 }}</Text>
    <View v-if="props.success" class="absolute left-[348] top-[108] w-[80] h-[80] rounded-full bg-black">
      <View class="absolute left-[4] top-[4] w-[72] h-[72] rounded-full bg-white" />
      <View class="absolute left-[20] top-[24] w-[8] h-[10] bg-black" />
      <View class="absolute left-[52] top-[24] w-[8] h-[10] bg-black" />
      <View class="absolute left-[18] top-[46] w-[6] h-[10] bg-black" />
      <View class="absolute left-[56] top-[46] w-[6] h-[10] bg-black" />
      <View class="absolute left-[24] top-[56] w-[32] h-[6] bg-black" />
    </View>
    <Text class="absolute left-[28] top-[652] text-2xl text-black">{{ props.hint }}</Text>
    <View class="absolute left-[28] top-[704] w-[424] h-[3] bg-black" />
    <Text class="absolute left-[28] top-[728] text-2xl text-black">{{ props.keys }}</Text>
    </View>
  </View>
</template>
