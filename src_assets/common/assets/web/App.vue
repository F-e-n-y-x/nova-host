<script setup>
import { computed } from 'vue'
import { useRoute } from 'vue-router'
import AppShell from './nova/AppShell.vue'

const route = useRoute()
const bare = computed(() => route.meta.bare === true)
</script>

<template>
  <template v-if="route.matched.length">
    <RouterView v-if="bare" />
    <AppShell v-else>
      <RouterView />
    </AppShell>
  </template>
</template>
