<script setup>
/**
 * Editor for host commands: the global `host_commands` setting, and an app's `menu-cmd` list in
 * the Library editor. Paired devices with the "Host commands" permission run them by id from
 * their stream menu (Foundation /supercmd); the command line itself never comes from a device.
 *
 * v-model: array of { id, name, icon, cmd, confirm, timeout }. New rows have an empty id; the
 * host derives one from the name when it saves, and keeps it afterwards.
 * Props: appIndex — the app's index in apps.json for per-app commands, null for global ones.
 */
import { computed, onBeforeUnmount, onMounted, shallowRef } from 'vue'
import { useI18n } from 'vue-i18n'
import {
  Folder, Gamepad2, Lock, MicOff, Monitor, Play, Plus, Power, RotateCw, Settings, Square, SquareTerminal, Trash2, Volume2,
} from '@lucide/vue'
import NvButton from '../../nova/components/NvButton.vue'
import NvIconButton from '../../nova/components/NvIconButton.vue'
import NvNumberField from '../../nova/components/NvNumberField.vue'
import NvSelect from '../../nova/components/NvSelect.vue'
import NvSwitch from '../../nova/components/NvSwitch.vue'
import NvTextField from '../../nova/components/NvTextField.vue'
import { fetchJson, postJson } from '../../nova/api'
import { commandLineError, HOST_COMMAND_ICONS, newHostCommand } from './hostCommands.js'

const model = defineModel({ type: Array, default: () => [] })
const props = defineProps({
  appIndex: { type: Number, default: null },
})
const { t } = useI18n()

const ICON_COMPONENTS = {
  terminal: SquareTerminal, refresh: RotateCw, power: Power, lock: Lock, volume: Volume2, 'mic-off': MicOff,
  monitor: Monitor, gamepad: Gamepad2, stop: Square, play: Play, folder: Folder, settings: Settings,
}

const commands = computed(() => (Array.isArray(model.value) ? model.value : []))
const iconOptions = computed(() => HOST_COMMAND_ICONS.map((value) => ({ value, label: t(`nova.settings.icon_${value}`) })))

/** Ids of the commands the host has saved, as {id → saved command line}; only those can run. */
const saved = shallowRef({})
/** Last run per id, from /api/host-commands/runs. */
const runs = shallowRef({})
const running = shallowRef('')
let pollTimer = null

function update(index, field, value) {
  model.value = commands.value.map((c, i) => (i === index ? { ...c, [field]: value } : c))
}

function add() {
  model.value = [...commands.value, newHostCommand()]
}

function remove(index) {
  model.value = commands.value.filter((_, i) => i !== index)
}

function nameError(command) {
  return command.name?.trim() ? '' : t('nova.settings.cmd_error_name')
}

function lineError(command) {
  return commandLineError(command.cmd) ? t('nova.settings.cmd_error_line') : ''
}

/** Whether a row can run as it is: saved on the host, and unchanged since. */
function canRun(command) {
  return !!command.id && saved.value[command.id] === command.cmd
}

async function loadSaved() {
  try {
    const body = await fetchJson('./api/host-commands')
    const list = props.appIndex === null ? body.global || [] : (body.apps || []).find((a) => a.index === props.appIndex)?.commands || []
    saved.value = Object.fromEntries(list.map((c) => [c.id, c.cmd]))
  } catch {
    saved.value = {}
  }
}

async function loadRuns() {
  try {
    const body = await fetchJson('./api/host-commands/runs')
    runs.value = Object.fromEntries((body.runs || []).map((r) => [r.id, r]))
  } catch {
    // Keep the last known results.
  }
}

async function run(command) {
  running.value = command.id
  try {
    await postJson('./api/host-commands/run', { id: command.id, app: props.appIndex })
  } catch {
    running.value = ''
    runs.value = { ...runs.value, [command.id]: { id: command.id, running: false, ok: false, error: t('nova.settings.cmd_run_failed', { name: command.name, error: '' }).trim() } }
    return
  }
  // Poll until the run finishes (the host enforces the command's own time limit).
  const started = Date.now()
  clearInterval(pollTimer)
  pollTimer = setInterval(async () => {
    await loadRuns()
    const record = runs.value[command.id]
    if ((record && !record.running) || Date.now() - started > 11 * 60 * 1000) {
      clearInterval(pollTimer)
      pollTimer = null
      running.value = ''
    }
  }, 1000)
}

/** Whether this row's run is in flight (unsaved rows have no id and never run). */
function isRunning(command) {
  return !!command.id && running.value === command.id
}

function resultText(command) {
  const record = runs.value[command.id]
  if (!record) return ''
  if (record.running) return t('nova.settings.cmd_running')
  if (record.error) return t('nova.settings.cmd_run_failed', { name: command.name, error: record.error })
  if (record.timed_out) return t('nova.settings.cmd_result_timeout', { name: command.name })
  if (record.ok) return t('nova.settings.cmd_result_ok', { name: command.name, ms: record.duration_ms })
  return t('nova.settings.cmd_result_fail', { name: command.name, code: record.exit_code })
}

onMounted(() => {
  // Nothing to run without rows; new rows can only run after they are saved (and the page reloads).
  if (commands.value.length === 0) return
  loadSaved()
  loadRuns()
})
onBeforeUnmount(() => clearInterval(pollTimer))
</script>

<template>
  <div class="nv-hostcmd">
    <p v-if="commands.length === 0" class="nv-hostcmd__empty">{{ t('nova.settings.cmd_empty') }}</p>
    <ol v-else class="nv-hostcmd__list">
      <li v-for="(command, index) in commands" :key="command.id || `new-${index}`" class="nv-hostcmd__item">
        <span class="nv-hostcmd__icon" aria-hidden="true">
          <component :is="ICON_COMPONENTS[command.icon] || SquareTerminal" :size="18" />
        </span>
        <div class="nv-hostcmd__fields">
          <NvTextField :model-value="command.name ?? ''" :label="t('nova.settings.cmd_name')"
                       :placeholder="t('nova.settings.cmd_name_placeholder')" :error="nameError(command)"
                       @update:model-value="(v) => update(index, 'name', v)" />
          <NvSelect :model-value="command.icon || 'terminal'" :label="t('nova.settings.cmd_icon')" :options="iconOptions"
                    @update:model-value="(v) => update(index, 'icon', v)" />
          <NvTextField class="nv-hostcmd__line" :model-value="command.cmd ?? ''" :label="t('nova.settings.cmd_line')" mono
                       :placeholder="t('nova.settings.cmd_line_placeholder')" :error="lineError(command)"
                       @update:model-value="(v) => update(index, 'cmd', v)" />
          <NvNumberField :model-value="command.timeout ?? 30" :label="t('nova.settings.cmd_timeout')"
                         :unit="t('nova.settings.cmd_timeout_unit')" :min="1" :max="600"
                         @update:model-value="(v) => update(index, 'timeout', v ?? 30)" />
          <label class="nv-hostcmd__confirm">
            <NvSwitch :model-value="!!command.confirm" :label="`${t('nova.settings.cmd_confirm')} ${index + 1}`"
                      @update:model-value="(v) => update(index, 'confirm', v)" />
            <span aria-hidden="true">{{ t('nova.settings.cmd_confirm') }}</span>
          </label>
          <div class="nv-hostcmd__run">
            <NvButton size="sm" :disabled="!canRun(command) || isRunning(command)"
                      :title="canRun(command) ? null : t('nova.settings.cmd_run_saved_only')" @click="run(command)">
              <Play :size="14" aria-hidden="true" />{{ isRunning(command) ? t('nova.settings.cmd_running') : t('nova.settings.cmd_run') }}
            </NvButton>
            <span v-if="command.id" class="nv-hostcmd__id">{{ t('nova.settings.cmd_id', { id: command.id }) }}</span>
            <span class="nv-hostcmd__result" aria-live="polite">{{ resultText(command) }}</span>
          </div>
          <details v-if="runs[command.id] && !runs[command.id].running" class="nv-hostcmd__output">
            <summary>{{ t('nova.settings.cmd_output') }}</summary>
            <pre>{{ runs[command.id].output || t('nova.settings.cmd_no_output') }}</pre>
          </details>
        </div>
        <NvIconButton :label="t('nova.settings.cmd_remove', { name: command.name || index + 1 })" @click="remove(index)">
          <Trash2 :size="16" />
        </NvIconButton>
      </li>
    </ol>
    <NvButton size="sm" @click="add"><Plus :size="16" aria-hidden="true" />{{ t('nova.settings.cmd_add') }}</NvButton>
  </div>
</template>

<style>
@layer components {
  .nv-hostcmd {
    display: flex;
    flex-direction: column;
    align-items: flex-start;
    gap: var(--nv-space-3);
  }

  .nv-hostcmd__empty {
    margin: 0;
    font-size: var(--nv-text-sm);
    color: var(--nv-text-secondary);
  }

  .nv-hostcmd__list {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-3);
    width: 100%;
    margin: 0;
    padding: 0;
    list-style: none;
  }

  .nv-hostcmd__item {
    display: flex;
    align-items: flex-start;
    gap: var(--nv-space-3);
    padding: var(--nv-space-3);
    border: 1px solid var(--nv-border);
    border-radius: var(--nv-radius-md);
    background: var(--nv-raised);
  }

  .nv-hostcmd__icon {
    display: flex;
    align-items: center;
    justify-content: center;
    flex-shrink: 0;
    width: 32px;
    height: 32px;
    margin-top: 26px;
    border-radius: var(--nv-radius-md);
    background: var(--nv-surface);
    color: var(--nv-text-secondary);
  }

  .nv-hostcmd__fields {
    display: grid;
    grid-template-columns: minmax(0, 2fr) minmax(0, 1fr);
    gap: var(--nv-space-3);
    flex-grow: 1;
    min-width: 0;
  }

  .nv-hostcmd__line {
    grid-column: 1 / -1;
  }

  .nv-hostcmd__confirm {
    display: flex;
    align-items: center;
    gap: var(--nv-space-2);
    align-self: end;
    min-height: 36px;
    font-size: var(--nv-text-sm);
  }

  .nv-hostcmd__run {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: var(--nv-space-2) var(--nv-space-3);
    grid-column: 1 / -1;
  }

  .nv-hostcmd__id {
    font-family: var(--nv-font-mono);
    font-size: var(--nv-text-xs);
    color: var(--nv-text-muted);
  }

  .nv-hostcmd__result {
    font-size: var(--nv-text-sm);
    color: var(--nv-text-secondary);
  }

  .nv-hostcmd__output {
    grid-column: 1 / -1;
    font-size: var(--nv-text-sm);
  }

  .nv-hostcmd__output pre {
    max-height: 240px;
    margin: var(--nv-space-2) 0 0;
    padding: var(--nv-space-2) var(--nv-space-3);
    overflow: auto;
    border-radius: var(--nv-radius-sm);
    background: var(--nv-surface);
    font-family: var(--nv-font-mono);
    font-size: var(--nv-text-xs);
    white-space: pre-wrap;
    overflow-wrap: anywhere;
  }

  .nv-hostcmd__item > .nv-icon-btn,
  .nv-hostcmd__item > button {
    margin-top: 26px;
  }

  @media (max-width: 699px) {
    .nv-hostcmd__fields {
      grid-template-columns: minmax(0, 1fr);
    }

    .nv-hostcmd__icon {
      display: none;
    }
  }
}
</style>
