<template>
  <nav class="navbar navbar-sunshine">
    <div class="container-fluid">
      <span class="navbar-brand d-flex align-items-center" title="Zenith">
        <img src="/images/logo-sunshine-45.png" height="36" alt="">
        <span class="ms-2">Zenith</span>
      </span>
      <ThemeToggle/>
    </div>
  </nav>
</template>

<script>
import ThemeToggle from './ThemeToggle.vue'

export default {
  components: {
    ThemeToggle,
  },
}
</script>
