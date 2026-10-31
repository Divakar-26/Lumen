# Events and pointer capture

`NodeStyle::event_handler` receives `UIEvent` values for pointer, wheel, keyboard, focus and click activity. Routing follows a DOM-like order: ancestors in capture phase, the target, then ancestors in bubble phase. The maximum current routing depth is bounded to protect the frame path buffer.

- `StopPropagation()` prevents later nodes/phases from receiving the event.
- `PreventDefault()` blocks the default behavior associated with the event. For `Click`, it suppresses the built-in button callback. For `PointerDown`/`PointerUp`, it suppresses click activation. For `Wheel`, it prevents Lumen's scroll handling for that wheel event.
- `CapturePointer()` must be called from the currently executing node's `PointerDown` handler or an active drag `PointerMove` handler. It routes later pointer moves and release to that node even outside the hit rectangle.
- `ReleasePointer()` must be called by the node that owns capture, during one of its callbacks. Removing the node from a later frame automatically clears its capture.
- Pointer ID `1` identifies the host's single mouse pointer. Multitouch and pointer IDs from external devices are not implemented.

Example:

```cpp
void OnDrag(Lumen::Context* ui, Lumen::UIEvent& event, void*) {
    switch (event.type) {
    case Lumen::UIEventType::PointerDown:
        (void)Lumen::CapturePointer(ui, event.current_target_id, event.pointer_id);
        break;
    case Lumen::UIEventType::PointerMove:
        if (Lumen::HasPointerCapture(ui, event.current_target_id, event.pointer_id)) {
            // Update application drag state here.
        }
        break;
    case Lumen::UIEventType::PointerUp:
        (void)Lumen::ReleasePointer(ui, event.current_target_id, event.pointer_id);
        break;
    default:
        break;
    }
}

Lumen::NodeStyle style{};
style.event_handler = OnDrag;
Lumen::BeginNode(ui, "draggable", style, "Drag me");
Lumen::EndNode(ui);
```

The input is sampled from the host-owned ImGui context; Lumen does not install native OS hooks or own the window/event loop. `GetNodeRect()` provides the last completed layout rectangle and returns false when that node was not in the most recently completed tree.
