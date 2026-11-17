/*
 * ActivityThread startup and process monitoring for DroidletLauncher.
 *
 * The launcher prepares the framework resources and installed application,
 * publishes the local service proxies, then lets the real ActivityThread run
 * its normal startup path.  The exit monitor watches for an empty task stack
 * and for services/jobs/broadcasts that still have work, and takes the
 * screenshot before exiting when a test asks for one.
 */

package android.app;

import java.io.File;
import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.ArrayList;
import java.util.List;

import static android.app.DroidletLauncher.*;
import static android.app.DroidletLauncherActivities.*;
import static android.app.DroidletLauncherActivityServices.*;
import static android.app.DroidletLauncherBroadcasts.*;
import static android.app.DroidletLauncherInspection.*;
import static android.app.DroidletLauncherInstall.*;
import static android.app.DroidletLauncherJobs.*;
import static android.app.DroidletLauncherServices.*;
import static android.app.DroidletLauncherWindows.*;
import static android.app.DroidletLauncherSystemServices.*;

final class DroidletLauncherStartup {
    private DroidletLauncherStartup() {}

    static void prepareFrameworkResources() {
        String root = System.getenv("ANDROID_ROOT");
        if (root == null || root.isEmpty()) {
            root = "/system";
        }

        File frameworkRes = new File(root, "framework/framework-res.apk");
        if (!frameworkRes.isFile()) {
            log("framework resources not found at " + frameworkRes);
            return;
        }

        String path = frameworkRes.getAbsolutePath();
        try {
            Class<?> assetManagerClass = Class.forName("android.content.res.AssetManager");
            Method create = assetManagerClass.getMethod(
                    "createSystemAssetsInZygoteLocked", boolean.class, String.class);
            create.invoke(null, true, path);
            log("initialized system assets from " + path);
        } catch (Throwable t) {
            log("unable to initialize system assets from " + path + ": " + t);
            try {
                patchFrameworkApkPath(path);
                log("patched AssetManager.FRAMEWORK_APK_PATH to " + path);
            } catch (Throwable t2) {
                log("unable to patch AssetManager.FRAMEWORK_APK_PATH: " + t2);
            }
        }
    }

    static void patchFrameworkApkPath(String path) throws Exception {
        Class<?> unsafeClass = Class.forName("sun.misc.Unsafe");
        Field theUnsafe = unsafeClass.getDeclaredField("theUnsafe");
        theUnsafe.setAccessible(true);
        Object unsafe = theUnsafe.get(null);

        Method staticFieldBase = unsafeClass.getMethod("staticFieldBase", Field.class);
        Method staticFieldOffset = unsafeClass.getMethod("staticFieldOffset", Field.class);
        Method putObject =
                unsafeClass.getMethod("putObject", Object.class, long.class, Object.class);

        Field field = Class.forName("android.content.res.AssetManager")
                .getDeclaredField("FRAMEWORK_APK_PATH");
        Object base = staticFieldBase.invoke(unsafe, field);
        long offset = (Long) staticFieldOffset.invoke(unsafe, field);
        putObject.invoke(unsafe, base, offset, path);
    }

    static void hostRequestClose() {
        log("host requested close");
        // The decoration close button closes the host window, not just the
        // task.  Do not let background jobs or bound services keep a black,
        // windowless process alive.
        sHostCloseRequested = true;
        postToActivityThread(new Runnable() {
            @Override
            public void run() {
                try {
                    finishAllActivities(0, null);
                } catch (Throwable t) {
                    log("host close failed: " + t);
                    System.exit(0);
                }
            }
        });
    }

    static void scheduleExitIfEmpty() {
        if (sServiceComponent != null) {
            // A service host exists only for its service.  Wait until the
            // ServiceRecord is actually removed by serviceDoneExecuting so
            // onUnbind/onDestroy always run before the process exits.
            boolean empty;
            synchronized (sServiceLock) {
                empty = sServiceByComponent.isEmpty();
            }
            if (empty) {
                log("service host: service is gone; exiting");
                System.out.flush();
                System.err.flush();
                System.exit(0);
            }
            return;
        }
        if (sExitScheduled || !sTaskStack.isEmpty()
                || (!sHostCloseRequested && (hasActiveServices() || hasPendingWork()))) {
            return;
        }
        sExitScheduled = true;
        postToActivityThread(new Runnable() {
            @Override
            public void run() {
                // Re-check on the app thread: a service may have started or
                // an activity may have been relaunched since this was posted.
                if (!sTaskStack.isEmpty()
                        || (!sHostCloseRequested
                                && (hasActiveServices() || hasPendingWork()))) {
                    sExitScheduled = false;
                    return;
                }
                log(sHostCloseRequested
                        ? "host close: exiting" : "task stack empty; exiting");
                System.out.flush();
                System.err.flush();
                System.exit(0);
            }
        });
    }

    static void prepareApplication() throws Exception {
        sBundleClass = Class.forName("android.os.Bundle");

        File dataDir = new File(sDataDir);
        mkdirs(dataDir);
        mkdirs(new File(dataDir, "cache"));
        mkdirs(new File(dataDir, "code_cache"));
        mkdirs(new File(dataDir, "files"));

        sUid = callStaticInt("android.os.Process", "myUid");

        Class<?> appInfoClass = Class.forName("android.content.pm.ApplicationInfo");
        Object appInfo = sParsedApplicationInfo;
        if (appInfo == null) {
            appInfo = appInfoClass.getDeclaredConstructor().newInstance();
        }

        int flags = appInfo == sParsedApplicationInfo
                ? (Integer) getFieldValue(appInfo, "flags")
                : 0;
        flags |= staticInt(appInfoClass, "FLAG_HAS_CODE", 1 << 2);
        flags |= staticInt(appInfoClass, "FLAG_SUPPORTS_SCREEN_DENSITIES", 1 << 14);
        flags |= staticInt(appInfoClass, "FLAG_INSTALLED", 1 << 23);
        if (sParsedApplicationInfo == null) {
            flags |= staticInt(appInfoClass, "FLAG_ALLOW_BACKUP", 1 << 15);
        }
        setField(appInfo, "packageName", sPackageName);
        setField(appInfo, "sourceDir", sApkPath);
        setField(appInfo, "publicSourceDir", sApkPath);
        setField(appInfo, "dataDir", sDataDir);
        setField(appInfo, "deviceProtectedDataDir", sDataDir);
        setField(appInfo, "credentialProtectedDataDir", sDataDir);
        setField(appInfo, "nativeLibraryDir", sNativeLibraryDir);
        setField(appInfo, "uid", sUid);
        setField(appInfo, "targetSdkVersion", sTargetSdk);
        setField(appInfo, "minSdkVersion", sMinSdk);
        setField(appInfo, "processName",
                sProcessName != null ? sProcessName : sPackageName);
        // ActivityThread/LoadedApk instantiate this class during
        // BIND_APPLICATION.  A null value means the stock Application.
        setField(appInfo, "className", sApplicationClass);
        setField(appInfo, "enabled", true);
        setField(appInfo, "flags", flags);
        if (sLabel != null) {
            setField(appInfo, "nonLocalizedLabel", sLabel);
            if (sResolvedActivityLabel == null) {
                sResolvedActivityLabel = sLabel;
            }
        } else {
            String resolved = resolveLabel((Integer) getFieldValue(appInfo, "labelRes"));
            if (resolved != null) {
                setField(appInfo, "nonLocalizedLabel", resolved);
            }
            Object appLabel = getFieldValue(appInfo, "nonLocalizedLabel");
            if (appLabel != null && sResolvedActivityLabel == null) {
                sResolvedActivityLabel = appLabel.toString();
            }
        }
        setField(appInfo, "theme", sApplicationTheme);
        if (getFieldValue(appInfo, "metaData") == null) {
            setField(appInfo, "metaData", newBundle());
        }
        if (sParsedApplicationInfo == null) {
            setField(appInfo, "appComponentFactory", null);
        }

        Class<?> activityInfoClass = Class.forName("android.content.pm.ActivityInfo");
        Object activityInfo = sParsedActivityInfo;
        if (activityInfo != null) {
            // PackageParser already filled in theme, launchMode, configChanges,
            // exported, parentActivityName, label/icon and the rest.
            setField(activityInfo, "packageName", sPackageName);
            setField(activityInfo, "name", sClassName);
            setField(activityInfo, "applicationInfo", appInfo);
            setField(activityInfo, "enabled", true);
            int activityFlags = (Integer) getFieldValue(activityInfo, "flags");
            setField(activityInfo, "flags", activityFlags);
            // Leave an explicit activity or application theme alone:
            // ActivityInfo.getThemeResource() already falls back to
            // applicationInfo.theme.  If the APK declares neither, use the
            // host default.  The platform default (DeviceDefault.Light with an
            // action bar) would work, but droidlet does not yet resolve action
            // bar labels; use the equivalent no-action-bar variant so legacy
            // apps keep their full content area.
            int labelRes = (Integer) getFieldValue(activityInfo, "labelRes");
            if (getFieldValue(activityInfo, "nonLocalizedLabel") == null && labelRes != 0) {
                String resolved = resolveLabel(labelRes);
                if (resolved != null) {
                    setField(activityInfo, "nonLocalizedLabel", resolved);
                    sResolvedActivityLabel = resolved;
                }
            }
            int theme = (Integer) getFieldValue(activityInfo, "theme");
            if (theme == 0 && sApplicationTheme == 0) {
                // Legacy apps used the device-default theme with a title/action
                // bar.  Newer apps keep the no-action-bar host default until
                // their action bar labels/insets are fully exercised.
                setField(activityInfo, "theme", sTargetSdk < 24 ? 0x0103012b : 0x0103012c);
            }
        } else {
            activityInfo = activityInfoClass.getDeclaredConstructor().newInstance();
            setField(activityInfo, "packageName", sPackageName);
            setField(activityInfo, "name", sClassName);
            setField(activityInfo, "applicationInfo", appInfo);
            setField(activityInfo, "exported", true);
            setField(activityInfo, "enabled", true);
            setField(activityInfo, "launchMode", 0);
            setField(activityInfo, "flags", 0);
            setField(activityInfo, "configChanges", 0);
            setField(activityInfo, "softInputMode", 0);
            setField(activityInfo, "uiOptions", 0);
            setField(activityInfo, "screenOrientation", -1);
            setField(activityInfo, "targetActivity", null);
            setField(activityInfo, "parentActivityName", null);
            setField(activityInfo, "permission", null);
            setField(activityInfo, "taskAffinity", null);
            setField(activityInfo, "theme",
                    sActivityTheme != 0 ? sActivityTheme : 0x01030241);
            setField(activityInfo, "labelRes", 0);
            setField(activityInfo, "nonLocalizedLabel", null);
        }

        sAppInfo = appInfo;
        sActivityInfo = activityInfo;
        if (sServiceInfo != null) {
            setField(sServiceInfo, "applicationInfo", appInfo);
        }
        prepareProviderInfos(appInfo);
    }

    static void prepareProviderInfos(Object appInfo) throws Exception {
        sProviderInfos = new ArrayList<>();
        if (sParsedPackage == null) {
            return;
        }
        List<?> providers = (List<?>) getFieldValue(sParsedPackage, "providers");
        for (Object provider : providers) {
            Object info = getFieldValue(provider, "info");
            if (info == null) {
                continue;
            }
            setField(info, "applicationInfo", appInfo);
            if (getFieldValue(info, "enabled") == Boolean.FALSE) {
                setField(info, "enabled", true);
            }
            sProviderInfos.add(info);
            log("provider: " + getFieldValue(info, "authority")
                    + " -> " + getFieldValue(info, "name"));
        }
        // Settings.Global/Secure/System need a content://settings provider.
        // There is no system_server, so install a host implementation in this
        // process before the manifest providers (AndroidX Startup reads
        // settings while its provider is being created).
        sProviderInfos.add(0, makeHostSettingsProviderInfo(appInfo));
    }

    static Object makeHostSettingsProviderInfo(Object appInfo) throws Exception {
        Object info = Class.forName("android.content.pm.ProviderInfo")
                .getDeclaredConstructor().newInstance();
        setField(info, "authority", "settings");
        setField(info, "name", "android.provider.DroidletSettingsProvider");
        setField(info, "packageName", sPackageName);
        setField(info, "applicationInfo", appInfo);
        setField(info, "exported", true);
        setField(info, "enabled", true);
        setField(info, "processName", sPackageName);
        setField(info, "directBootAware", true);
        setField(info, "isSyncable", false);
        setField(info, "multiprocess", false);
        setField(info, "initOrder", 0);
        log("provider: settings -> android.provider.DroidletSettingsProvider");
        return info;
    }

    static void installCompatibilityRules() throws Exception {
        Class<?> arrayClass = Class.forName("android.util.LongSparseArray");
        Object rules = arrayClass.getDeclaredConstructor().newInstance();
        Class<?> infoClass =
                Class.forName("com.android.internal.compat.CompatibilityChangeInfo");
        Constructor<?> ctor = infoClass.getConstructor(Long.class, String.class, int.class,
                int.class, boolean.class, boolean.class, boolean.class, String.class,
                boolean.class);
        Method put = arrayClass.getMethod("put", long.class, Object.class);

        // droidlet has no system bars and does not provide edge-to-edge
        // insets, so keep the legacy fitSystemWindows layout for every app.
        addCompatRule(put, rules, ctor, 309578419L,
                "ENFORCE_EDGE_TO_EDGE", -1, -1, true);
        addCompatRule(put, rules, ctor, 377864165L,
                "DISABLE_OPT_OUT_EDGE_TO_EDGE", -1, 36, false);
        // @Disabled in PhoneWindow.
        addCompatRule(put, rules, ctor, 332679525L,
                "OVERRIDE_LAYOUT_IN_DISPLAY_CUTOUT_MODE", -1, -1, true);

        Class.forName("com.android.internal.compat.CompatibilityRules")
                .getMethod("init", arrayClass).invoke(null, rules);
        log("installed compatibility rules");
    }

    static void addCompatRule(Method put, Object rules, Constructor<?> ctor,
            long changeId, String name, int enabledAfter, int enabledSince, boolean disabled)
            throws Exception {
        put.invoke(rules, changeId, ctor.newInstance(Long.valueOf(changeId), name,
                enabledAfter, enabledSince, disabled, false, false, null, false));
    }

    static void installServices() throws Exception {
        // Load the host media JNI early so AudioTrack natives are available
        // even when an app never touches MediaPlayer/MediaMetadataRetriever.
        try {
            System.loadLibrary("media_jni");
        } catch (Throwable t) {
            log("unable to load media_jni: " + t);
        }
        // SystemSensorManager calls nativeClassInit() from its constructor.
        // The real sensors JNI needs a sensor HAL, so register the host stubs
        // that report an empty sensor list.
        try {
            System.loadLibrary("droidlet_sensors_jni");
        } catch (Throwable t) {
            log("unable to load droidlet_sensors_jni: " + t);
        }
        // flutter_secure_storage and other apps expect a software
        // AndroidKeyStore.  There is no keystore daemon, so install the
        // in-process compatibility providers before the app starts.
        try {
            java.security.Security.addProvider((java.security.Provider) Class
                    .forName("android.security.DroidletKeyStoreProvider")
                    .getDeclaredConstructor().newInstance());
            java.security.Security.addProvider((java.security.Provider) Class
                    .forName("android.security.DroidletKeyStoreBCWorkaroundProvider")
                    .getDeclaredConstructor().newInstance());
            log("installed AndroidKeyStore compatibility providers");
        } catch (Throwable t) {
            log("unable to install AndroidKeyStore providers: " + t);
            t.printStackTrace(System.err);
            if (t.getCause() != null) {
                t.getCause().printStackTrace(System.err);
            }
        }
        // Configure software/hardware rendering before ActivityThread loads the
        // app classes.  hwui may initialize RenderThread from app code before
        // the first window is added.
        configureThreadedRenderer();
        installCompatibilityRules();
        // WindowSessionHandler creates the app's InputChannel when the window
        // is added, so the presenter library must already be loaded.
        loadPresenterLibrary();

        sIApplicationThreadClass = Class.forName("android.app.IApplicationThread");
        sClientTransactionClass =
                Class.forName("android.app.servertransaction.ClientTransaction");
        sClientTransactionItemClass =
                Class.forName("android.app.servertransaction.ClientTransactionItem");
        sBundleClass = Class.forName("android.os.Bundle");

        registerLocalService("activity",
                "android.app.IActivityManager", new ActivityManagerHandler());
        registerLocalService("package",
                "android.content.pm.IPackageManager", new PackageManagerHandler());

        Class<?> accInterface = Class.forName("android.app.IActivityClientController");
        sActivityClientController = Proxy.newProxyInstance(
                accInterface.getClassLoader(),
                new Class<?>[] {accInterface},
                new ActivityClientControllerHandler());
        // ActivityClient obtains the controller through
        // ActivityTaskManager.getService().getActivityClientController(); serve
        // that from a local activity_task service instead of poking the
        // ActivityClient singleton directly.
        registerLocalService("activity_task",
                "android.app.IActivityTaskManager", new ActivityTaskManagerHandler());

        // "display" is required by ContextImpl/DisplayManagerGlobal, "user" by
        // ContextImpl.getDisplayNoVerifyInner(), and "window" by ViewConfiguration
        // while the decor view is being constructed.
        registerLocalService("display",
                "android.hardware.display.IDisplayManager", new DisplayManagerHandler());
        registerLocalService("user", "android.os.IUserManager", new UserManagerHandler());
        registerLocalService("window",
                "android.view.IWindowManager", new WindowManagerHandler());
        // Glmark2Activity takes a screen wake lock in onCreate, and
        // PowerManager's SystemServiceRegistry fetcher also resolves the
        // thermal service eagerly.  A missing "power" service makes
        // getSystemService() throw and the activity die with a
        // NullPointerException on the wake lock.
        registerLocalService("power",
                "android.os.IPowerManager", new PowerManagerHandler());
        // Context.THERMAL_SERVICE is "thermalservice", not "thermal".
        registerLocalService("thermalservice",
                "android.os.IThermalService", new ThermalServiceHandler());
        // WindowManagerGlobal.getWindowSession() initializes the default
        // InputMethodManager, which refuses to construct without an
        // IInputMethodManager service.
        registerLocalService("input_method",
                "com.android.internal.view.IInputMethodManager",
                new InputMethodManagerHandler());
        // ContentResolver resolves this in its static initializer.  ListView
        // and every CursorAdapter constructor registers a content observer, so
        // a missing service is a NullPointerException before the first frame.
        registerLocalService("content",
                "android.content.IContentService", new ContentServiceHandler());
        // Toast.getService()/NotificationManager.getService() return this
        // directly; a missing service makes Toast.show() throw.
        registerLocalService("notification",
                "android.app.INotificationManager", new NotificationManagerHandler());
        // Compose's ComposeView casts getSystemService(UI_MODE_SERVICE) to
        // UiModeManager without a null check, and Theme code reads contrast.
        registerLocalService("uimode",
                "android.app.IUiModeManager", new UiModeManagerHandler());
        // AppWidgetManager.getInstance() returns null without this, and both
        // Jetsnack and Jetchat treat it as non-null during first composition.
        registerLocalService("appwidget",
                "com.android.internal.appwidget.IAppWidgetService",
                new AppWidgetManagerHandler());
        // View.performClick() plays a sound effect, and ViewRootImpl asks
        // AudioManager whether navigation-repeat effects are enabled.  Without
        // this service the first click on any View kills the process.
        registerLocalService("audio",
                "android.media.IAudioService", new AudioManagerHandler());
        // SDLActivity's HIDDeviceManager calls UsbManager.getDeviceList() from
        // onCreate; a missing "usb" service makes getSystemService() return
        // null and the app dies with a NullPointerException.
        registerLocalService("usb",
                "android.hardware.usb.IUsbManager", new UsbManagerHandler());
        // PermissionManager's constructor resolves this service eagerly (it is
        // reached through ApplicationPackageManager.checkPermission, which SDL
        // calls for its Bluetooth HID path).
        registerLocalService("legacy_permission",
                "android.permission.ILegacyPermissionManager",
                new LegacyPermissionManagerHandler());
        // Context.getExternalFilesDir() asks StorageManager to mkdir/fixup the
        // app-private directory; libGDX calls it while creating its files
        // backend (Edge Roll), and other apps use StorageManager volumes.
        registerLocalService("mount",
                "android.os.storage.IStorageManager", new StorageManagerHandler());
        // AppCompat reads LocaleManager.getApplicationLocales() when an
        // activity is created; a missing "locale" service logs a
        // ServiceNotFoundException and returns a null LocaleList.
        registerLocalService("locale",
                "android.app.ILocaleManager", new LocaleManagerHandler());
        // MediaSessionCompat creates a framework MediaSession in the player
        // service; MediaSessionManager resolves ISessionManager eagerly and
        // MediaSession dereferences its createSession() result.
        registerLocalService("media_session",
                "android.media.session.ISessionManager", new MediaSessionManagerHandler());
        // BatteryManager's fetcher resolves both of these; NewPipe reads
        // BATTERY_PROPERTY_CAPACITY to detect a Fire TV during onCreate.
        registerLocalService("batterystats",
                "com.android.internal.app.IBatteryStats", new BatteryStatsHandler());
        registerLocalService("batteryproperties",
                "android.os.IBatteryPropertiesRegistrar", new BatteryPropertiesHandler());
        // KeyEvent.getUnicodeChar() resolves a KeyCharacterMap through
        // InputManagerGlobal, which needs an input service and a virtual
        // keyboard device.
        registerLocalService("input",
                "android.hardware.input.IInputManager", new InputManagerHandler());
        // AndroidX Startup/WorkManager read ConnectivityManager while the
        // initializer provider is created.  A single fake internet-capable
        // network is enough for the host.
        registerLocalService("connectivity",
                "android.net.IConnectivityManager", new ConnectivityManagerHandler());
        // WorkManager schedules through JobScheduler and AlarmManager even when
        // it later runs work in-process.
        registerLocalService("jobscheduler",
                "android.app.job.IJobScheduler", new JobSchedulerHandler());
        registerLocalService("alarm",
                "android.app.IAlarmManager", new AlarmManagerHandler());
        // ClipboardManager and AccountManager construct their managers eagerly
        // in some AndroidX code paths.
        registerLocalService("clipboard",
                "android.content.IClipboard", new ClipboardHandler());
        // Fenix asks for ShortcutManager during HomeActivity startup; return
        // an empty shortcut host so shortcut APIs become no-ops.
        registerLocalService("shortcut",
                "android.content.pm.IShortcutService", new ShortcutServiceHandler());
        // Fenix probes the default-browser role during onboarding.
        registerLocalService("role",
                "android.app.role.IRoleManager", new RoleManagerHandler());
        registerLocalService("account",
                "android.accounts.IAccountManager", new AccountManagerHandler());
        registerLocalService("appops",
                "com.android.internal.app.IAppOpsService", new AppOpsHandler());

        log("registered local binder services for " + sPackageName + "/" + sClassName);
    }

    static Object registerLocalService(
            String serviceName, String interfaceName, InvocationHandler handler)
            throws Exception {
        Class<?> iface = Class.forName(interfaceName);
        Object local = Proxy.newProxyInstance(
                iface.getClassLoader(), new Class<?>[] {iface}, handler);
        // AIDL-generated Java types declare DESCRIPTOR on the Stub class, not on
        // the interface itself.
        String descriptor;
        try {
            descriptor = (String) Class.forName(interfaceName + "$Stub")
                    .getField("DESCRIPTOR").get(null);
        } catch (ClassNotFoundException | NoSuchFieldException e) {
            descriptor = interfaceName;
        }

        Class<?> binderClass = Class.forName("android.os.Binder");
        Object binder = binderClass.getDeclaredConstructor().newInstance();
        binderClass.getMethod("attachInterface",
                        Class.forName("android.os.IInterface"), String.class)
                .invoke(binder, local, descriptor);

        // android.os.IServiceManager.DUMP_FLAG_PRIORITY_DEFAULT.
        int dumpPriority = 8;
        Class.forName("android.os.ServiceManager")
                .getMethod("addService", String.class,
                        Class.forName("android.os.IBinder"), boolean.class, int.class)
                .invoke(null, serviceName, binder, false, dumpPriority);
        return binder;
    }

    static void launchActivityThread() throws Exception {
        Class<?> atClass = Class.forName("android.app.ActivityThread");
        Method main = atClass.getMethod("main", String[].class);
        log("starting ActivityThread");
        main.invoke(null, (Object) new String[0]);
    }

    static void onAttachApplication(Object appThread) throws Exception {
        if (sScheduled) {
            log("attachApplication called again; ignoring");
            return;
        }
        sScheduled = true;
        sAppThread = appThread;
        // ActivityThread.main() calls Environment.initForCurrentUser(), so
        // install the host external storage view after that and before the
        // application/activity is created.
        installExternalStorage();

        Class<?> atClass = Class.forName("android.app.ActivityThread");
        Object thread = atClass.getMethod("currentActivityThread").invoke(null);
        if (thread == null) {
            throw new IllegalStateException("ActivityThread.currentActivityThread() is null");
        }

        // The real bindApplication path delivers core settings via
        // IApplicationThread.setCoreSettings().  We construct the same shape
        // here before posting BIND_APPLICATION.
        setField(thread, "mCoreSettings", coreSettings());
        installApplicationSharedMemory();

        Object data = makeAppBindData(thread);
        postBindApplication(thread, data);
        if (sServiceComponent != null) {
            postServiceHostStart(thread);
            log("queued BIND_APPLICATION and service-host start");
            return;
        }
        ActivityRecord initial = createInitialRecord();
        initial.launched = true;
        scheduleLaunch(initial);
        log("queued BIND_APPLICATION and LaunchActivityItem");

        if (System.getenv("DROIDLET_EXIT_AFTER_LAUNCH") != null) {
            postMilestoneExit(thread);
        } else {
            // The launch transaction has to run before the resume transaction:
            // the activity record and its PhoneWindow only exist once
            // performLaunchActivity() has completed.  Posting a runnable on the
            // same handler gives us that ordering point, and lets us adjust the
            // window attributes before handleResumeActivity() tries to attach a
            // window.
            postResumeAfterLaunch(thread, initial);
        }
    }

    static void postMilestoneExit(Object thread) throws Exception {
        Object handler = Class.forName("android.app.ActivityThread")
                .getMethod("getHandler").invoke(thread);
        Runnable exit = new Runnable() {
            @Override
            public void run() {
                log("launch milestone reached; exiting");
                System.out.flush();
                System.err.flush();
                System.exit(0);
            }
        };
        Class.forName("android.os.Handler")
                .getMethod("post", Runnable.class)
                .invoke(handler, exit);
    }

    static void postResumeAfterLaunch(final Object thread,
            final ActivityRecord initial) throws Exception {
        Object handler = Class.forName("android.app.ActivityThread")
                .getMethod("getHandler").invoke(thread);
        Runnable resume = new Runnable() {
            @Override
            public void run() {
                try {
                    prepareWindowForResume(thread);
                    scheduleResume(initial);
                    initial.resumed = true;
                    sResumedRecord = initial;
                    log("queued ResumeActivityItem");
                } catch (Throwable t) {
                    log("unable to schedule resume: " + t);
                    t.printStackTrace(System.err);
                    System.exit(1);
                }
                try {
                    postLifecycleCheck(thread);
                } catch (Throwable t) {
                    log("unable to post lifecycle check: " + t);
                }
            }
        };
        Class.forName("android.os.Handler")
                .getMethod("post", Runnable.class)
                .invoke(handler, resume);
    }

    static void prepareWindowForResume(Object thread) throws Exception {
        Object record = findActivityRecord(thread);
        if (record == null) {
            log("activity record not found; continuing without window tweaks");
            return;
        }
        Object activity = getFieldValue(record, "activity");
        if (activity == null) {
            log("activity record has no Activity; continuing");
            return;
        }
        if (sResolvedActivityLabel != null) {
            try {
                Object actionBar = activity.getClass().getMethod("getActionBar").invoke(activity);
                if (actionBar != null) {
                    actionBar.getClass().getMethod("setTitle", CharSequence.class)
                            .invoke(actionBar, sResolvedActivityLabel);
                }
            } catch (Throwable t) {
                log("unable to set action bar title: " + t);
            }
        }
        Object window = activity.getClass().getMethod("getWindow").invoke(activity);
        Object attrs = window.getClass().getMethod("getAttributes").invoke(window);
        int features = (Integer) getFieldValue(attrs, "inputFeatures");
        int noInput = staticInt(
                Class.forName("android.view.WindowManager$LayoutParams"),
                "INPUT_FEATURE_NO_INPUT_CHANNEL", 1);
        features &= ~noInput;
        setField(attrs, "inputFeatures", features);
        log("window prepared for resume (inputFeatures=0x"
                + Integer.toHexString(features) + ")");
    }

    static void configureThreadedRenderer() {
        try {
            Class<?> rendererClass = Class.forName("android.view.ThreadedRenderer");
            Field enabled = rendererClass.getDeclaredField("sRendererEnabled");
            enabled.setAccessible(true);
            if (!enabled.getBoolean(null)) {
                enabled.setBoolean(null, true);
            }
            Class.forName("android.graphics.HardwareRenderer")
                    .getMethod("setIsolatedProcess", boolean.class)
                    .invoke(null, true);
            log("hardware rendering enabled (ThreadedRenderer.sRendererEnabled=true)");
        } catch (Throwable t) {
            log("unable to configure ThreadedRenderer: " + t);
        }
    }

    static Object findActivityRecord(Object thread) throws Exception {
        Field activitiesField = findField(thread.getClass(), "mActivities");
        activitiesField.setAccessible(true);
        Object activities = activitiesField.get(thread);
        if (activities == null || sActivityToken == null) {
            return null;
        }
        Method get = activities.getClass().getMethod("get", Object.class);
        return get.invoke(activities, sActivityToken);
    }

    static void postLifecycleCheck(final Object thread) throws Exception {
        Object handler = Class.forName("android.app.ActivityThread")
                .getMethod("getHandler").invoke(thread);
        Runnable check = new Runnable() {
            @Override
            public void run() {
                int state = lifecycleState(thread);
                log("activity lifecycle state=" + stateName(state));
                try {
                    logWindowState(thread);
                } catch (Throwable t) {
                    log("unable to inspect window state: " + t);
                }
                if (state == 3 /* ON_RESUME */) {
                    scheduleScreenshot(thread, 0);
                    scheduleExitMonitor(thread);
                } else if (System.getenv("DROIDLET_EXIT_AFTER_RESUME") != null) {
                    System.out.flush();
                    System.err.flush();
                    System.exit(1);
                }
            }
        };
        Class.forName("android.os.Handler")
                .getMethod("postDelayed", Runnable.class, long.class)
                .invoke(handler, check, 500L);
    }

    static void scheduleExitMonitor(final Object thread) {
        final String frameText = System.getenv("DROIDLET_EXIT_AFTER_FRAME");
        final boolean afterScript = System.getenv("DROIDLET_EXIT_AFTER_SCRIPT") != null;
        if (sExitMonitorStarted) {
            return;
        }
        if ((frameText == null || frameText.isEmpty()) && !afterScript) {
            return;
        }
        sExitMonitorStarted = true;
        final long frameTarget;
        try {
            frameTarget = frameText == null || frameText.isEmpty()
                    ? 0 : Long.parseLong(frameText.trim());
        } catch (NumberFormatException e) {
            log("invalid DROIDLET_EXIT_AFTER_FRAME: " + frameText);
            return;
        }
        final long timeoutMs = envLong("DROIDLET_EXIT_TIMEOUT_MS", 30000);
        final long startNanos = System.nanoTime();
        try {
            final Object handler = Class.forName("android.app.ActivityThread")
                    .getMethod("getHandler").invoke(thread);
            Runnable check = new Runnable() {
                @Override
                public void run() {
                    boolean done = false;
                    if (frameTarget > 0) {
                        try {
                            long[] stats = nativeHostFrameStats();
                            done = stats != null && stats.length >= 3 && stats[1] >= frameTarget;
                        } catch (Throwable t) {
                            log("exit monitor: frame stats unavailable: " + t);
                        }
                    }
                    if (!done && afterScript) {
                        try {
                            done = nativeInputScriptFinished();
                        } catch (Throwable t) {
                            log("exit monitor: input script state unavailable: " + t);
                        }
                    }
                    if (done) {
                        log("exit monitor: reached target");
                        System.out.flush();
                        System.err.flush();
                        System.exit(0);
                        return;
                    }
                    long elapsedMs = (System.nanoTime() - startNanos) / 1000000L;
                    if (timeoutMs > 0 && elapsedMs >= timeoutMs) {
                        log("exit monitor: timed out after " + elapsedMs + " ms");
                        System.out.flush();
                        System.err.flush();
                        System.exit(1);
                        return;
                    }
                    try {
                        Class.forName("android.os.Handler")
                                .getMethod("postDelayed", Runnable.class, long.class)
                                .invoke(handler, this, 100L);
                    } catch (Throwable t) {
                        log("exit monitor: unable to reschedule: " + t);
                    }
                }
            };
            Class.forName("android.os.Handler")
                    .getMethod("postDelayed", Runnable.class, long.class)
                    .invoke(handler, check, 100L);
        } catch (Throwable t) {
            log("unable to start exit monitor: " + t);
        }
    }

    static boolean isSet(String name) {
        String value = System.getenv(name);
        return value != null && !value.isEmpty();
    }

    static long envLong(String name, long fallback) {
        String value = System.getenv(name);
        if (value == null || value.isEmpty()) {
            return fallback;
        }
        try {
            return Long.parseLong(value);
        } catch (NumberFormatException e) {
            return fallback;
        }
    }

    private static int sDisplayWidth;
    private static int sDisplayHeight;
    private static int sDisplayDensityDpi;

    static int displayWidth() {
        if (sDisplayWidth <= 0) {
            sDisplayWidth = Math.max(1, envInt("DROIDLET_DISPLAY_WIDTH", 1280));
        }
        return sDisplayWidth;
    }

    static int displayHeight() {
        if (sDisplayHeight <= 0) {
            sDisplayHeight = Math.max(1, envInt("DROIDLET_DISPLAY_HEIGHT", 800));
        }
        return sDisplayHeight;
    }

    static int displayDensityDpi() {
        if (sDisplayDensityDpi <= 0) {
            int dpi = envInt("DROIDLET_DISPLAY_DPI", 160);
            sDisplayDensityDpi = Math.max(72, Math.min(dpi, 640));
        }
        return sDisplayDensityDpi;
    }

    static int pxToDp(int px) {
        return Math.round(px * 160.0f / displayDensityDpi());
    }

    /*
     * The compositor resized the host window.  Keep the synthetic display in
     * sync, then let ActivityThread apply the new configuration and ask every
     * ViewRootImpl to relayout against the new display size.
     */
    private static int sDisplayConfigSeq = 1;

    static void displayResized(final int width, final int height) {
        if (width <= 0 || height <= 0) {
            return;
        }
        if (width == displayWidth() && height == displayHeight()) {
            return;
        }
        log("display resized to " + width + "x" + height
                + " @" + displayDensityDpi() + "dpi");
        setDisplaySize(width, height);
        DroidletLauncherWindows.updateDisplaySize(width, height);
        postToActivityThread(new Runnable() {
            @Override
            public void run() {
                try {
                    Class<?> configClass = Class.forName("android.content.res.Configuration");
                    Object config = makeConfiguration();
                    // ConfigurationController ignores a config whose seq is not
                    // newer than the current one; the launch config uses 0.
                    setField(config, "seq", ++sDisplayConfigSeq);
                    Class<?> threadClass = Class.forName("android.app.ActivityThread");
                    Object thread = threadClass.getMethod("currentActivityThread").invoke(null);
                    threadClass.getMethod("handleConfigurationChanged", configClass, int.class)
                            .invoke(thread, config, 0);
                    Class<?> globalClass = Class.forName("android.view.WindowManagerGlobal");
                    Object global = globalClass.getMethod("getInstance").invoke(null);
                    globalClass.getMethod("reportNewConfiguration", configClass)
                            .invoke(global, config);

                    // reportNewConfiguration only queues MSG_UPDATE_CONFIGURATION,
                    // which goes through the activity override config and may
                    // be a no-op when the app handles configChanges itself.
                    // Drive each ViewRootImpl directly so the window relayouts
                    // against the new display size.
                    java.lang.reflect.Field rootsField = globalClass.getDeclaredField("mRoots");
                    rootsField.setAccessible(true);
                    Object value = rootsField.get(global);
                    int relayouts = 0;
                    if (value instanceof java.util.List) {
                        for (Object root : (java.util.List<?>) value) {
                            if (root == null) {
                                continue;
                            }
                            try {
                                setField(root, "mForceNextWindowRelayout", true);
                            } catch (Throwable ignored) {
                            }
                            try {
                                root.getClass().getMethod("updateConfiguration", int.class)
                                        .invoke(root, -1 /* INVALID_DISPLAY */);
                                root.getClass().getMethod("requestLayout").invoke(root);
                                relayouts++;
                            } catch (Throwable t) {
                                log("display resize relayout failed: " + t);
                            }
                        }
                    }
                    try {
                        Class<?> threadClass2 = Class.forName("android.app.ActivityThread");
                        Object appThread = threadClass2.getMethod("currentActivityThread")
                                .invoke(null);
                        java.lang.reflect.Field activitiesField =
                                threadClass2.getDeclaredField("mActivities");
                        activitiesField.setAccessible(true);
                        Object activities = activitiesField.get(appThread);
                        if (activities instanceof java.util.Map) {
                            for (Object record : ((java.util.Map<?, ?>) activities).values()) {
                                Object activity = getFieldValue(record, "activity");
                                if (activity == null) {
                                    continue;
                                }
                                // The global config update above changes the
                                // process resources but not the activity's own
                                // Resources/config override.  ActivityThread
                                // normally receives handleActivityConfigurationChanged
                                // from ActivityTaskManager; run that path so the
                                // activity's resources and window see the new
                                // size before we relayout.
                                Object windowInfo = getFieldValue(record,
                                        "mActivityWindowInfo");
                                if (windowInfo == null) {
                                    continue;
                                }
                                Object override = configClass.getDeclaredConstructor()
                                        .newInstance();
                                setField(override, "seq", sDisplayConfigSeq);
                                java.lang.reflect.Method changed = threadClass2
                                        .getDeclaredMethod("handleActivityConfigurationChanged",
                                                record.getClass(), configClass, int.class,
                                                windowInfo.getClass(), boolean.class);
                                changed.setAccessible(true);
                                changed.invoke(thread, record, override, -1, windowInfo, true);
                                Object ares = activity.getClass().getMethod("getResources")
                                        .invoke(activity);
                                Object ac = ares.getClass().getMethod("getConfiguration")
                                        .invoke(ares);
                                log("display resize activity: wDp="
                                        + getFieldValue(ac, "screenWidthDp")
                                        + " hDp=" + getFieldValue(ac, "screenHeightDp")
                                        + " smallest="
                                        + getFieldValue(ac, "smallestScreenWidthDp"));
                            }
                        }
                    } catch (Throwable t) {
                        log("display resize resource probe failed: " + t);
                    }
                    log("display resize configuration applied (roots=" + relayouts + ")");
                } catch (Throwable t) {
                    log("display resize configuration failed: " + t);
                    t.printStackTrace(System.err);
                }
            }
        });
    }

    static void setDisplaySize(int width, int height) {
        sDisplayWidth = Math.max(1, width);
        sDisplayHeight = Math.max(1, height);
    }

    static int envInt(String name, int fallback) {
        long value = envLong(name, fallback);
        if (value <= 0 || value > 16384) {
            return fallback;
        }
        return (int) value;
    }

    static boolean composerActive() {
        return isSet("DROIDLET_COMPOSER");
    }

    static boolean loadPresenterLibrary() {
        if (sPresenterLoadFailed) {
            return false;
        }
        if (sPresenterLoadAttempted) {
            return true;
        }
        sPresenterLoadAttempted = true;

        ArrayList<File> candidates = new ArrayList<>();
        String explicit = System.getenv("DROIDLET_PRESENTER_LIB");
        if (explicit != null && !explicit.isEmpty()) {
            candidates.add(new File(explicit));
        }
        String prefix = System.getenv("PREFIX");
        if (prefix != null && !prefix.isEmpty()) {
            candidates.add(new File(prefix, "lib/x86_64-linux-gnu/libdroidlet_presenter.so"));
            candidates.add(new File(prefix, "lib/libdroidlet_presenter.so"));
        }
        for (File candidate : candidates) {
            if (!candidate.isFile()) {
                continue;
            }
            try {
                System.load(candidate.getAbsolutePath());
                log("presenter library: " + candidate.getAbsolutePath());
                return true;
            } catch (Throwable t) {
                log("presenter: unable to load " + candidate + ": " + t);
            }
        }
        try {
            System.loadLibrary("droidlet_presenter");
            log("presenter library: found via java.library.path");
            return true;
        } catch (Throwable t) {
            log("presenter library not available: " + t);
        }
        sPresenterLoadFailed = true;
        return false;
    }

    static Object enumConstant(Class<?> enumClass, String name) {
        for (Object constant : enumClass.getEnumConstants()) {
            if (name.equals(String.valueOf(constant))) {
                return constant;
            }
        }
        throw new IllegalArgumentException(name + " not in " + enumClass.getName());
    }

    static void installApplicationSharedMemory() throws Exception {
        Class<?> asmClass =
                Class.forName("com.android.internal.os.ApplicationSharedMemory");
        Object sharedMemory = asmClass.getMethod("create").invoke(null);
        asmClass.getMethod("setInstance", asmClass).invoke(null, sharedMemory);

        // A freshly created region has no feature data.  Mark every SDK feature
        // as unavailable instead of handing SystemFeaturesCache a zero-length
        // array, which it rejects.
        int featureCount = Class.forName("android.content.pm.PackageManager")
                .getField("SDK_FEATURE_COUNT").getInt(null);
        int[] featureCache = new int[featureCount];
        java.util.Arrays.fill(featureCache, Integer.MIN_VALUE);
        Class<?> cacheClass = Class.forName("android.content.pm.SystemFeaturesCache");
        if (!(Boolean) cacheClass.getMethod("hasInstance").invoke(null)) {
            Object cache = cacheClass.getConstructor(int[].class).newInstance(featureCache);
            cacheClass.getMethod("setInstance", cacheClass).invoke(null, cache);
        }

        asmClass.getMethod("setCurrentAnimatorScale", float.class)
                .invoke(sharedMemory, 1.0f);
        log("initialized ApplicationSharedMemory");
    }

    static Object coreSettings() throws Exception {
        Object root = newBundle();
        Object defaultDevice = newBundle();
        Method putBundle = sBundleClass.getMethod(
                "putBundle", String.class, sBundleClass);
        putBundle.invoke(root, "0", defaultDevice);
        return root;
    }

    static Object makeAppBindData(Object thread) throws Exception {
        Class<?> dataClass = Class.forName("android.app.ActivityThread$AppBindData");
        Constructor<?> ctor = dataClass.getDeclaredConstructor();
        ctor.setAccessible(true);
        Object data = ctor.newInstance();

        setField(data, "processName",
                sProcessName != null ? sProcessName : sPackageName);
        setField(data, "appInfo", sAppInfo);
        setField(data, "providers", sProviderInfos != null
                ? sProviderInfos : new ArrayList<Object>());
        setField(data, "config", makeConfiguration());
        setField(data, "compatInfo", compatibilityInfo());
        setField(data, "buildSerial", "unknown");
        setField(data, "initProfilerInfo", null);
        setField(data, "instrumentationName", null);
        setField(data, "instrumentationArgs", null);
        setField(data, "instrumentationWatcher", null);
        setField(data, "instrumentationUiAutomationConnection", null);
        setField(data, "debugMode", 0);
        setField(data, "enableBinderTracking", false);
        setField(data, "trackAllocation", false);
        setField(data, "restrictedBackupMode", false);
        setField(data, "persistent", false);
        setField(data, "isSdkInSandbox", false);
        setField(data, "sdkSandboxClientAppVolumeUuid", null);
        setField(data, "sdkSandboxClientAppPackage", null);
        setField(data, "autofillOptions", null);
        setField(data, "contentCaptureOptions", null);
        setField(data, "disabledCompatChanges", null);
        setField(data, "enabledCompatChanges", null);
        setField(data, "mLoggableCompatChanges", null);
        setField(data, "mLogChangeChecksToStatsD", false);
        setField(data, "mSerializedSystemFontMap", null);
        setField(data, "startRequestedElapsedTime", 0L);
        setField(data, "startRequestedUptime", 0L);
        return data;
    }

    static void postBindApplication(Object thread, Object data) throws Exception {
        Object handler = Class.forName("android.app.ActivityThread")
                .getMethod("getHandler").invoke(thread);
        Class<?> handlerClass = Class.forName("android.os.Handler");
        int what = Class.forName("android.app.ActivityThread$H")
                .getDeclaredField("BIND_APPLICATION").getInt(null);

        Method obtain = handlerClass.getMethod("obtainMessage", int.class, Object.class);
        Object message = obtain.invoke(handler, what, data);
        Class.forName("android.os.Message").getMethod("sendToTarget").invoke(message);
    }

}
