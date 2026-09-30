// Nova browser client: runs before moonlight-web-stream's stream.js (a classic script, so it
// executes while the page parses). It lets Nova's overlay see every stream event from the very
// first one: when upstream subscribes to its Stream's "stream-info" events, a recorder is added to
// the same target, and nova-ui/stream.js replays what it missed. Nothing else is touched.
(function () {
  'use strict'
  var add = EventTarget.prototype.addEventListener
  var hub = { events: [], listeners: [] }
  window.__novaStreamInfo = hub
  EventTarget.prototype.addEventListener = function (type, listener, options) {
    if (type === 'stream-info' && !this.__novaRecorded) {
      Object.defineProperty(this, '__novaRecorded', { value: true })
      add.call(this, 'stream-info', function (event) {
        hub.events.push(event)
        for (var i = 0; i < hub.listeners.length; i++) {
          try { hub.listeners[i](event) } catch (e) { console.error(e) }
        }
      })
    }
    return add.call(this, type, listener, options)
  }
})()
