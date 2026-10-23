# Native application modules

An executable can optionally link a `NativeApplicationModule`. AxEng binds the
module to the main Lua state, calls `initialize` before the Lua entry point, and
calls `shutdown` before application resources are released. Lua-only
applications do not need to provide a native module.

The module owns its event subscriptions. During initialization it can subscribe
to any engine event exposed by the application, for example:

```cpp
const auto update_id{ app.window()->get_update_event_handler().subscribe(
    [this](const ax::WindowUpdateEvent& event) { update(event.delta); }) };
```

Store subscription IDs and unsubscribe them in `shutdown` before the window is
destroyed. AxEng does not prescribe a fixed update/render callback list; modules
choose which engine events they need.

Consumers that enable the generic native-module host define
`USE_NATIVE_MODULE` and `NATIVE_MODULE_HEADER`. The selected header declares
`std::unique_ptr<ax::NativeApplicationModule>
create_native_application_module();`. The consumer target compiles and links its
own module implementation with `AxEngLib`. Project-specific targets and build
configuration belong in the consumer's build files, not AxEng.

Native modules are trusted in-process C++ code and are not a security sandbox.
