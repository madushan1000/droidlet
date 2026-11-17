# Framework overlay

This directory contains host-build replacements for framework classes that
cannot run unmodified in the droidlet process.

The overlay is compiled into `framework-overlay.jar` and installed to
`share/droidlet`.  `droidlet` prepends it to `BOOTCLASSPATH`, so a class in this
overlay shadows the same class in the prebuilt `framework.jar`.

## Layout

```
android/app/DroidletLauncher.java      launcher entry point
android/net/TrafficStats.java          packaged overlay class
compile_stubs/android/content/Context.java   javac-only dependency
```

Put packaged classes under their normal package path at the top level of this
directory.  Put compile-only stand-ins under `compile_stubs/`; those are not
included in the installed dex jar.

## Current overlays

- `android.net.TrafficStats`: avoids the eager
  `libframework-connectivity-tiramisu-jni` load in the real class.  Statistics
  are reported as `UNSUPPORTED`; socket tagging is a no-op.
- `android.graphics.fonts.SystemFonts`: resolves the system font config and
  font directory from `ANDROID_ROOT` instead of the hardcoded `/system` paths,
  so the install tree can be used without a mount namespace.
- `android.os.Trace`: no-op tracing entry points, because the host build does
  not register `register_android_os_Trace`.
- `android.content.pm.PackagePartitions`: derives the fingerprint from
  `Build.FINGERPRINT` instead of the missing native crypto digest, so
  `OverlayConfig` can read the partition list.
- `android.window.WindowExtensionsHelper`: returns `false` when no window
  extensions provider is installed instead of dereferencing null.
- `com.android.internal.os.DebugStore`: drops diagnostic events because the
  Rust debugstore JNI bridge is not built.
- `com.android.org.bouncycastle.jce.provider.BouncyCastleProvider`: registers
  the provider name with no algorithms so `Providers` initialization succeeds
  without the full BC jar.
- `android.app.DroidletLauncher`: the in-process launcher.  It is the entry
  point (`droidlet` invokes it) and is described below.  It is not a shadow of
  a framework class.

## In-process launcher

`DroidletLauncher` parses the APK with the real
`android.content.pm.PackageParser` (the installer's parser), installs it under
`$ANDROID_DATA/app/<package>/base.apk`, and then starts the real
`ActivityThread.main()`.  Services are real `android.os.Binder` objects
published through the real `ServiceManager` into droidlet's process-local
registry; each carries its AIDL implementation as an attached local interface.

| Service | Interface | Shim behaviour |
| --- | --- | --- |
| `activity` | `IActivityManager` | Posts `BIND_APPLICATION`, then schedules the launch and resume `ClientTransaction`s. |
| `activity_task` | `IActivityTaskManager` | Returns the `IActivityClientController` implementation for `ActivityClient`. |
| `package` | `IPackageManager` | Returns the installed package/activity info, including the manifest's custom `Application` class. |
| `display` | `IDisplayManager` | Returns one default display with `sDisplayId`. |
| `user` | `IUserManager` | Reports the user as unlocked. |
| `window` | `IWindowManager` | Returns a local `IWindowSession` from `openSession()`. |
| `input_method` | `IInputMethodManager` | No-op `addClient()`/`reportFullscreenMode()`. |
| `content` | `IContentService` | Tracks content observers; there are no providers, so notifications are no-ops. |
| `notification` | `INotificationManager` | Accepts and logs toast enqueues; other notification calls return defaults. |
| `uimode` | `IUiModeManager` | Normal UI mode, night mode auto, contrast 0; required by Compose. |
| `appwidget` | `IAppWidgetService` | No installed widgets; pinning is unsupported. |
| `audio` | `IAudioService` | No sound effects; `View.performClick()` asks about navigation-repeat sounds. |
| `input` | `IInputManager` | One virtual keyboard device backed by `$ANDROID_ROOT/usr/keychars/Virtual.kcm`; needed by `KeyCharacterMap`. |
| `permissionmgr` | `IPermissionManager` | Empty split-permission table; `PackageParser` queries it while installing. |

Registration goes through `ServiceManager.addService()`, which resolves to
`BackendUnifiedServiceManager`'s local map because droidlet publishes that
backend as binder handle 0 before the VM starts.  Lookups need no kernel
driver, and same-process AIDL calls go through
`Binder.queryLocalInterface()` to the attached Java implementation.

### Window session

`IWindowSession` is a single local proxy for the process.  `addToDisplay()` /
`addToDisplayAsUser()` and `relayout()` / `relayout2()` return `0`
(`WindowManagerGlobal.ADD_OKAY`) and produce no `SurfaceControl` and no
`InputChannel`.  `ViewRootImpl` therefore completes `setView()` and
`performTraversals()`, but its render target stays invalid and it skips drawing.
That is enough for the lifecycle milestone; real surfaces still need a display
compositor and gralloc.

The activity window sets `INPUT_FEATURE_NO_INPUT_CHANNEL` before resume so
`ViewRootImpl.setView()` does not construct a native `InputEventReceiver`.
The `input_method` shim exists because
`WindowManagerGlobal.getWindowSession()` initializes the default
`InputMethodManager` first.

### Buffer allocator preload

`ViewRootImpl` eagerly warms up hwui's `GraphicBufferAllocator` on a shared
thread (`ThreadedRenderer.preInitBufferAllocator()`).  That constructs the
gralloc mapper and aborts with `gralloc-mapper is missing` when no gralloc HAL
is present.  The launcher sets `ViewRootImpl.sPreInitializedBufferAllocator`
before the first `ViewRootImpl` is built, so the warm-up is skipped.  Nothing on
the headless path allocates a buffer, so this is safe.

## Adding a class

1. Add the Java source under the appropriate package directory.
2. Add it to `framework_overlay_classes` in `meson.build`.
3. Add any Android types it references but the host JDK does not have to
   `compile_stubs/` and to `framework_overlay_compile_stubs`.
4. Rebuild and install.  `droidlet` will put the overlay jar first in
   `BOOTCLASSPATH` automatically.

Keep overlays small and binary-compatible with the methods the framework calls.
A partial stub is acceptable only when the missing methods are never reached.
