/*
 * In-process Activity launcher for droidlet.
 *
 * This class is installed on BOOTCLASSPATH (share/droidlet/framework-overlay.jar)
 * and therefore shares the boot class loader and the android.app runtime package
 * with the real ActivityThread.
 *
 * The launcher receives an APK path from the droidlet binary, reads its
 * AndroidManifest.xml, installs it under $ANDROID_DATA/app/<package>, and then
 * drives the real ActivityThread startup path:
 *
 *   ActivityThread.main()
 *     -> attach(false)
 *       -> ActivityManager.getService().attachApplication(...)
 *         -> our local IActivityManager shim
 *           -> post H.BIND_APPLICATION (AppBindData)
 *           -> schedule ClientTransaction { LaunchActivityItem }
 *           -> schedule ClientTransaction { ResumeActivityItem }
 *
 * The local IActivityManager, IApplicationThread, IPackageManager,
 * IActivityClientController and IWindowManager/IWindowSession are
 * java.lang.reflect.Proxy instances, so they need no binder driver and no
 * generated AIDL stubs.  The actual framework classes are only touched through
 * reflection, which keeps this overlay independent of the host javac classpath.
 *
 * The larger shims live next to this file, package-private in android.app:
 *
 *   DroidletLauncherInstall           PackageParser install, manifest queries,
 *                                     native library extraction, external storage
 *   DroidletLauncherStartup           ActivityThread startup, service proxy
 *                                     registration, exit/fd monitoring
 *   DroidletLauncherActivities        task stack and lifecycle transactions
 *   DroidletLauncherActivityServices  IActivityManager, IPackageManager,
 *                                     ITaskManager, IActivityClientController
 *   DroidletLauncherServices          start/bind/stop, service host process
 *   DroidletLauncherBroadcasts        receiver registry, ordered broadcasts,
 *                                     sticky broadcasts, PendingIntent records
 *   DroidletLauncherJobs              alarm scheduling and JobScheduler
 *   DroidletLauncherSystemServices    the small per-service proxies (audio,
 *                                     clipboard, power, USB, notifications...)
 *   DroidletLauncherWindows           WindowManager/WindowSession/display,
 *                                     input channels and the dialog dim layer
 *   DroidletLauncherInspection        screenshot capture and diagnostics
 *
 * This class keeps main()/argument parsing, package query helpers that the
 * dispatchers share, and the small reflection utilities used by every file.
 */

package android.app;

import android.content.Intent;
import android.os.Binder;
import android.os.Parcel;

import java.io.File;
import java.lang.reflect.Field;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

import static android.app.DroidletLauncherActivities.*;
import static android.app.DroidletLauncherActivityServices.*;
import static android.app.DroidletLauncherBroadcasts.*;
import static android.app.DroidletLauncherInspection.*;
import static android.app.DroidletLauncherInstall.*;
import static android.app.DroidletLauncherJobs.*;
import static android.app.DroidletLauncherServices.*;
import static android.app.DroidletLauncherStartup.*;
import static android.app.DroidletLauncherSystemServices.*;
import static android.app.DroidletLauncherWindows.*;

public final class DroidletLauncher {
    private static final String TAG = "DroidletLauncher";

    static final int MATCH_DIRECT_BOOT_UNAWARE = 0x00040000;
    static final int MATCH_DIRECT_BOOT_AWARE = 0x00080000;
    static final int MATCH_DISABLED_COMPONENTS = 0x00000200;

    static String sRequestedComponent;
    static String sPackageName;
    static String sClassName;
    static String sApplicationClass;
    static int sApplicationTheme;
    static int sActivityTheme;
    static String sApkPath;
    static String sNativeLibraryDir;
    static String sLabel;
    static String sDataDir;
    static String sScreenshotPath;
    static boolean sExitMonitorStarted;
    private static int sStartupFds = -1;
    static final ArrayList<Integer> sWindowInputChannels = new ArrayList<>();
    static final Map<Object, Integer> sWindowChannelByWindow =
            new IdentityHashMap<>();
    static final Map<Integer, float[]> sWindowChannelGeometry = new HashMap<>();
    static final Map<Integer, float[]> sWindowChannelInsets = new HashMap<>();
    static final Map<Integer, Integer> sWindowChannelType = new HashMap<>();
    static final Map<Integer, Integer> sWindowChannelLayerId = new HashMap<>();
    // ViewRootImpl omits the LayoutParams once it has sent them (relayout with
    // a null attrs argument).  WindowManagerService keeps the last window
    // params; do the same so match_parent windows follow display resizes.
    static final Map<Object, Object> sWindowAttrsByWindow = new IdentityHashMap<>();
    static final Map<Object, Integer> sWindowLayerId = new IdentityHashMap<>();
    static int sActiveInputChannel;
    private static Object sPackageInstaller;
    static int sTargetSdk = 1;
    static int sMinSdk = 1;
    static int sUid = 1000;
    static int sDisplayId = 0;

    static Object sAppInfo;
    static Object sActivityInfo;
    static Object sAppThread;
    static ActivityRecord sResumedRecord;
    static boolean sExitScheduled;
    static volatile boolean sHostCloseRequested;
    static final ArrayList<ActivityRecord> sTaskStack = new ArrayList<>();
    static Object sParsedPackage;
    static Object sParsedApplicationInfo;
    static Object sParsedActivityInfo;
    static Object sAppResources;
    static String sResolvedActivityLabel;
    static ArrayList<Object> sProviderInfos;
    static Object sActivityClientController;
    static Object sDefaultDisplayInfo;
    static Object sActivityToken;
    static Object sDimLayer;
    static boolean sDimVisible;
    static Object sWindowSession;
    static boolean sScheduled;
    static boolean sPresenterLoadAttempted;
    static boolean sPresenterLoadFailed;

    static Class<?> sClientTransactionClass;
    static Class<?> sClientTransactionItemClass;
    static Class<?> sIApplicationThreadClass;
    static Class<?> sBundleClass;

    // In-app broadcasts, pending intents, alarms and jobs.  There is no
    // system server: the launcher delivers these to components of the
    // launched package only.
    static final Object sBroadcastLock = new Object();
    static final ArrayList<RegisteredReceiver> sRegisteredReceivers =
            new ArrayList<>();
    static final ArrayList<Object> sStickyBroadcasts = new ArrayList<>();

    static final Object sPendingIntentLock = new Object();
    static final Map<Object, PendingIntentRecord> sPendingIntents =
            new IdentityHashMap<>();

    // Service-host mode (child side): run one manifest service in this
    // process and publish its onBind() binder over binder RPC.
    static String sServiceComponent;
    static int sServiceRpcFd = -1;
    static Object sServiceInfo;
    static String sProcessName;

    private DroidletLauncher() {}

    public static void main(String[] args) {
        Runtime.getRuntime().addShutdownHook(new Thread(new Runnable() {
            @Override
            public void run() {
                logExitStats();
            }
        }, "droidlet-exit-stats"));
        try {
            parseArgs(args);
            prepareFrameworkResources();
            installApplication();
            prepareApplication();
            sStartupFds = countOpenFds();
            installServices();
            if (sServiceComponent != null) {
                launchServiceHostThread();
            } else {
                launchActivityThread();
            }
        } catch (Throwable t) {
            log("fatal: " + t);
            t.printStackTrace(System.err);
            System.exit(1);
        }
    }

    // ---------------------------------------------------------------------
    // Framework resource bootstrap
    // ---------------------------------------------------------------------

    /**
     * AssetManager resolves the framework resources through the hardcoded
     * /system/framework/framework-res.apk path.  droidlet runs from an install
     * tree, so initialize the system assets from $ANDROID_ROOT before anything
     * asks for Resources.getSystem().  Once sSystem is populated, the framework
     * never consults the hardcoded path again.
     */

    /**
     * Last-resort fallback for older framework builds that do not expose
     * createSystemAssetsInZygoteLocked().  FRAMEWORK_APK_PATH is not a
     * compile-time constant, so later readers observe the patched value.
     */

    // ---------------------------------------------------------------------
    // Argument handling
    // ---------------------------------------------------------------------

    private static void parseArgs(String[] args) {
        String apk = null;
        String data = null;

        for (int i = 0; i < args.length; i++) {
            String arg = args[i];
            if ("--".equals(arg)) {
                // Remaining arguments belong to the application.
                break;
            } else if ("--apk".equals(arg) && i + 1 < args.length) {
                apk = args[++i];
            } else if ("--activity".equals(arg) && i + 1 < args.length) {
                sRequestedComponent = args[++i];
            } else if ("--data".equals(arg) && i + 1 < args.length) {
                data = args[++i];
            } else if ("--service".equals(arg) && i + 1 < args.length) {
                sServiceComponent = args[++i];
            } else if ("--rpc-fd".equals(arg) && i + 1 < args.length) {
                sServiceRpcFd = Integer.parseInt(args[++i]);
            } else if ("--display-id".equals(arg) && i + 1 < args.length) {
                sDisplayId = Integer.parseInt(args[++i]);
            } else if ("--screenshot".equals(arg) && i + 1 < args.length) {
                sScreenshotPath = new File(args[++i]).getAbsolutePath();
            } else if ("--no-screenshot".equals(arg)) {
                sScreenshotPath = "";
            } else if (arg.startsWith("--")) {
                log("ignoring unknown option " + arg);
            }
        }

        if (apk == null || apk.isEmpty()) {
            throw new IllegalArgumentException("an APK path is required");
        }
        sApkPath = new File(apk).getAbsolutePath();

        if (data != null && !data.isEmpty()) {
            sDataDir = new File(data).getAbsolutePath();
        }
    }

    // ---------------------------------------------------------------------
    // APK manifest / installation
    // ---------------------------------------------------------------------




    /*
     * Parse the APK with the real android.content.pm.PackageParser.  This is
     * the same parser PackageManagerService uses during install, so themes,
     * labels, icons, activity attributes, providers and permissions all come
     * from the framework's manifest grammar instead of a home-grown parser.
     */




    /*
     * PackageManager.hasSystemFeature() normally asks the system-feature cache
     * first; the droidlet overlay bypasses that cache so every feature lands
     * here.  Report the capabilities the Wayland/X11 host actually provides.
     */

    /*
     * Resolve label resources from the installed APK.  PackageParser keeps the
     * resource id for @string/@style labels; ActivityInfo.loadLabel() would
     * normally ask PackageManagerService.getText(), which droidlet does not
     * implement.  Populate nonLocalizedLabel here so action bars and titles
     * show the app/activity name.
     */



    static Object findParsedActivityComponent(Object pkg, String className) throws Exception {
        if (pkg == null || className == null) {
            return null;
        }
        List<?> activities = (List<?>) getFieldValue(pkg, "activities");
        for (Object activity : activities) {
            Object info = getFieldValue(activity, "info");
            if (className.equals(getFieldValue(info, "name"))) {
                return activity;
            }
        }
        return null;
    }





    // ---------------------------------------------------------------------
    // In-process task stack
    // ---------------------------------------------------------------------

    /*
     * Entry point for the compositor's decoration close button.  libdroidlet
     * resolves this exact class/method with JNI GetStaticMethodID, so it must
     * stay on DroidletLauncher even though the implementation lives in
     * DroidletLauncherStartup.
     */
    static void hostRequestClose() {
        DroidletLauncherStartup.hostRequestClose();
    }

    /*
     * Entry point for a compositor-driven window resize.  Like
     * hostRequestClose(), libdroidlet's presenter resolves it with JNI
     * GetStaticMethodID on this class.
     */
    static void hostDisplayResized(int width, int height) {
        DroidletLauncherStartup.displayResized(width, height);
    }

    static void postToActivityThread(Runnable runnable) {
        try {
            Class<?> atClass = Class.forName("android.app.ActivityThread");
            Object thread = atClass.getMethod("currentActivityThread").invoke(null);
            Object handler = atClass.getMethod("getHandler").invoke(thread);
            Class.forName("android.os.Handler")
                    .getMethod("post", Runnable.class).invoke(handler, runnable);
        } catch (Throwable t) {
            log("unable to post to ActivityThread: " + t);
        }
    }


    static ActivityRecord findRecordByToken(Object token) {
        if (token == null) {
            return null;
        }
        for (ActivityRecord record : sTaskStack) {
            if (token == record.token || token.equals(record.token)) {
                return record;
            }
        }
        return null;
    }




    static boolean intentFilterMatches(Object filter, Object intent) throws Exception {
        Class<?> intentClass = intent.getClass();
        String action = (String) intentClass.getMethod("getAction").invoke(intent);
        String type = (String) intentClass.getMethod("getType").invoke(intent);
        String scheme = (String) intentClass.getMethod("getScheme").invoke(intent);
        Object data = intentClass.getMethod("getData").invoke(intent);
        Set<?> categories = (Set<?>) intentClass.getMethod("getCategories").invoke(intent);
        int match = (Integer) filter.getClass()
                .getMethod("match", String.class, String.class, String.class,
                        Class.forName("android.net.Uri"), Set.class, String.class)
                .invoke(filter, action, type, scheme, data, categories, "droidlet");
        return match >= 0;
    }

    static Object makeResolveInfo(Object componentInfo, Object filter,
            String componentField) throws Exception {
        Object info = Class.forName("android.content.pm.ResolveInfo")
                .getDeclaredConstructor().newInstance();
        setField(info, componentField, componentInfo);
        if (filter != null) {
            setField(info, "filter", filter);
        }
        setField(info, "priority", 0);
        setField(info, "preferredOrder", 0);
        setField(info, "match", 0);
        setField(info, "isDefault", false);
        return info;
    }

    static Object findParsedService(Object pkg, String className) throws Exception {
        if (pkg == null || className == null) return null;
        List<?> services = (List<?>) getFieldValue(pkg, "services");
        for (Object service : services) {
            Object info = getFieldValue(service, "info");
            if (className.equals(getFieldValue(info, "name"))) return service;
        }
        return null;
    }

    static Object findParsedProvider(Object pkg, String className) throws Exception {
        if (pkg == null || className == null) return null;
        List<?> providers = (List<?>) getFieldValue(pkg, "providers");
        for (Object provider : providers) {
            Object info = getFieldValue(provider, "info");
            if (className.equals(getFieldValue(info, "name"))) return provider;
        }
        return null;
    }

    static Object findParsedReceiver(Object pkg, String className) throws Exception {
        if (pkg == null || className == null) return null;
        List<?> receivers = (List<?>) getFieldValue(pkg, "receivers");
        for (Object receiver : receivers) {
            Object info = getFieldValue(receiver, "info");
            if (className.equals(getFieldValue(info, "name"))) return receiver;
        }
        return null;
    }

    static Object findParsedProviderByAuthority(Object pkg, String authority)
            throws Exception {
        if (pkg == null || authority == null) return null;
        List<?> providers = (List<?>) getFieldValue(pkg, "providers");
        for (Object provider : providers) {
            Object info = getFieldValue(provider, "info");
            if (authority.equals(getFieldValue(info, "authority"))) return provider;
        }
        return null;
    }

    /*
     * Turn a parsed PackageParser component into the public *Info object the
     * way PackageManagerService does.  The generated object carries the
     * component metadata (GET_META_DATA), URI permission patterns and the
     * application info; returning the raw parsed component drops those.
     */
    static Object generateComponentInfo(String kind, Object component, int flags)
            throws Exception {
        if (component == null) {
            return null;
        }
        Class<?> parserClass = Class.forName("android.content.pm.PackageParser");
        Class<?> stateClass = Class.forName("android.content.pm.pkg.FrameworkPackageUserState");
        Class<?> componentClass = Class.forName("android.content.pm.PackageParser$" + kind);
        Object state = stateClass.getField("DEFAULT").get(null);
        int generateFlags = flags | MATCH_DIRECT_BOOT_AWARE
                | MATCH_DIRECT_BOOT_UNAWARE | MATCH_DISABLED_COMPONENTS;
        Method generate = parserClass.getMethod("generate" + kind + "Info",
                componentClass, int.class, stateClass, int.class);
        return generate.invoke(null, component, generateFlags, state, 0);
    }





    /*
     * Called from the presenter when the compositor's decoration close button
     * is pressed.  Finish the whole task, which also exits the process.
     */














    // ---------------------------------------------------------------------
    // ApplicationInfo / ActivityInfo
    // ---------------------------------------------------------------------


    /*
     * ActivityThread.installContentProviders() consumes ProviderInfo objects
     * from AppBindData before Application.onCreate().  PackageParser already
     * produced them; point them at the host ApplicationInfo and preserve
     * manifest order.
     */


    /*
     * Zygote normally preloads the platform compatibility rules before any
     * app process exists.  droidlet has no zygote, so CompatibilityRules.sRules
     * stays empty and every unknown change falls back to "enabled".  That is
     * how ENFORCE_EDGE_TO_EDGE leaked into targetSdk < 35 apps: PhoneWindow
     * made every window edge-to-edge, and the action bar overlaid the content
     * instead of insetting it.
     *
     * Seed the rules that matter to the window decor.  Unknown changes keep
     * the framework's "enabled" default, matching the current host behavior.
     */


    // ---------------------------------------------------------------------
    // Service shims
    // ---------------------------------------------------------------------


    /*
     * Publish a process-local service through the real ServiceManager.
     *
     * The service is a genuine android.os.Binder so that it has a stable binder
     * identity for Parcel/service-manager round trips.  The concrete AIDL
     * implementation is a dynamic proxy attached as the Binder's local
     * interface: same-process Java callers get it directly from
     * Binder.queryLocalInterface(), so normal AIDL calls become virtual calls,
     * while the Binder still supports transact() if anything serializes it.
     *
     * droidlet installs its BackendUnifiedServiceManager as binder handle 0
     * before the VM starts, so ServiceManager never needs a kernel driver.
     */

    // ---------------------------------------------------------------------
    // ActivityThread startup
    // ---------------------------------------------------------------------


    /*
     * Service host: there is no activity and no launcher transaction.  The
     * same ActivityThread main loop is used, but onAttachApplication posts
     * startServiceHost() instead of a LaunchActivityItem.
     */




    /*
     * Called from the native RPC root when the parent asks this child host to
     * stop.  Hop to the app thread so onUnbind/onDestroy and the regular
     * service teardown run before the process exits.
     */





    static native Object nativeStartServiceHost(String apk, String dataDir,
            String component);

    static native void nativeStopServiceHost(Object binder);

    static native boolean nativeServiceHostSetRoot(Object binder, int fd);

    static native boolean nativeIsServiceHostAlive(Object binder);


    /*
     * The launch transaction is dispatched through the same ActivityThread H
     * queue as BIND_APPLICATION.  Posting a Runnable after it guarantees the
     * runnable executes only once performLaunchActivity (and therefore the
     * app's onCreate/setContentView) has returned.  This is used to end the
     * first milestone deterministically, before onResume needs a real window.
     */

    /*
     * Second milestone: drive the activity from ON_CREATE to ON_START and
     * ON_RESUME and attach its decor view to a (headless) window session.
     *
     * handleLaunchActivity() deliberately stops at onCreate(), so resume needs
     * a separate transaction.  Before submitting it we mark the PhoneWindow as
     * not wanting an input channel: ViewRootImpl otherwise constructs a real
     * InputEventReceiver, which needs the native input stack and a window
     * manager that can hand out a channel.  With the flag set, setView() skips
     * the input pipeline entirely.
     */

    /*
     * Make sure the activity window asks ViewRootImpl for an InputChannel.
     * Activity.getWindow() is already a PhoneWindow by this point, and
     * getAttributes() returns the very same LayoutParams instance that
     * handleResumeActivity() copies into ViewRootImpl.
     */

    /*
     * Enable hwui's ThreadedRenderer.  droidlet's host EGL manager renders into
     * an offscreen pbuffer and copies the result into the in-process window, so
     * the BufferQueue-backed surface path is not used.
     */



    /*
     * Bounded CI runs: --exit-after-frame and --exit-after-script let the
     * harness stop as soon as the requested work is done instead of relying on
     * a fixed sleep.  A timeout turns a stuck run into a test failure.
     */

    /*
     * The resume transaction has completed by the time the lifecycle check
     * runs, but ViewRootImpl's first traversal is asynchronous.  Retry until
     * the composer has published a frame instead of assuming a fixed delay is
     * enough.
     */


    /*
     * Copies the frame the composer last presented.  That frame is the result of
     * ViewRootImpl.drawSoftware() into a real Surface, not a re-rendered decor:
     * the composer latched the BLASTBufferQueue buffer and blitted it into its
     * staging frame.  Runs on the ActivityThread handler, but only blocks while
     * the composer publishes a frame; the first traversal has already run by the
     * time ON_RESUME is reported.
     */

    // ---------------------------------------------------------------------
    // Native presenter library (composer JNI)
    // ---------------------------------------------------------------------




    /*
     * True when the native launcher started the in-process composer that owns
     * presentation.  startHostComposer() exports DROIDLET_COMPOSER=1 before the
     * Java runtime starts.
     */








    /*
     * ActivityThread.bindApplication() normally creates the process-wide
     * ApplicationSharedMemory and SystemFeaturesCache before queuing
     * BIND_APPLICATION.  We post BIND_APPLICATION directly, so do that setup
     * here; WindowManagerGlobal and ApplicationPackageManager assume it exists.
     */











    static Object newBinder() throws Exception {
        return Class.forName("android.os.Binder").getDeclaredConstructor().newInstance();
    }

    static Object newBundle() throws Exception {
        return sBundleClass.getDeclaredConstructor().newInstance();
    }

    // ---------------------------------------------------------------------
    // Dynamic proxy handlers
    // ---------------------------------------------------------------------



    static Object defaultCompatibilityInfo() throws Exception {
        return Class.forName("android.content.res.CompatibilityInfo")
                .getField("DEFAULT_COMPATIBILITY_INFO").get(null);
    }

    static int serviceProcessState() {
        try {
            return staticInt(Class.forName("android.app.ActivityManager"),
                    "PROCESS_STATE_SERVICE", 6);
        } catch (Throwable t) {
            return 6;
        }
    }


    static Object appThreadInvoke(String method, Class<?>[] types, Object... args)
            throws Exception {
        if (sAppThread == null) {
            throw new IllegalStateException("application thread is not attached");
        }
        Method target = sIApplicationThreadClass.getMethod(method, types);
        target.setAccessible(true);
        return target.invoke(sAppThread, args);
    }

    /*
     * Resolve a service intent against the launched package's manifest.
     * Cross-package components are rejected: droidlet has no cross-app service
     * registry and no service manager daemon.
     */

    /*
     * A same-app service that declares android:process (or isolatedProcess)
     * runs in a child droidlet process and is reached over binder RPC.  Only
     * binding is supported for these services.
     */
















    /*
     * Bind to a service that runs in its own droidlet process.  Child startup
     * takes 1-2 s, so it happens on a background thread; the client's
     * IServiceConnection.connected() is called once the RPC root arrives.
     */





















    /*
     * RPC root for a service host.  Normal transactions are forwarded to the
     * Service.onBind() binder.  Private transactions start/stop the service
     * and are handled on the child's app thread.
     */



    static boolean intentEquals(Object left, Object right) {
        if (left == null || right == null) {
            return left == right;
        }
        try {
            return (Boolean) left.getClass().getMethod("filterEquals",
                    Class.forName("android.content.Intent")).invoke(left, right);
        } catch (Throwable t) {
            return left == right;
        }
    }











































    static String componentField(Object component, String method) {
        try {
            return (String) component.getClass().getMethod(method).invoke(component);
        } catch (Throwable t) {
            return null;
        }
    }


    /*
     * The host has no package installer service.  Returning a non-null proxy
     * keeps PackageInstaller's null check happy and lets apps that only read
     * session state initialise; real installs are not supported yet.
     */
    static Object packageInstaller() {
        if (sPackageInstaller == null) {
            try {
                Class<?> iface = Class.forName("android.content.pm.IPackageInstaller");
                sPackageInstaller = Proxy.newProxyInstance(iface.getClassLoader(),
                        new Class<?>[] {iface}, new PackageInstallerHandler());
            } catch (Throwable t) {
                log("unable to create PackageInstaller: " + t);
            }
        }
        return sPackageInstaller;
    }



    /*
     * ViewRootImpl allocates an empty InputChannel for addToDisplay and passes
     * it as an out parameter.  Create the pair natively, then copy the client
     * end into that object.  copyTo() duplicates the native channel so the
     * temporary Java wrapper can be collected.
     */

    /*
     * Focus reaches ViewRootImpl as an InputEventReceiver FOCUS message, not
     * through an IWindow method.  The message is buffered in the channel until
     * setView() installs the receiver.
     */

    /*
     * A single local WindowManagerGlobal session is enough for this process.
     * addToDisplay reports success and installs the app's InputChannel;
     * relayout fills in the window frames.  ViewRootImpl owns its
     * SurfaceControl and BLASTBufferQueue, so the session does not create
     * buffers itself.
     */


    /*
     * Fills the out WindowRelayoutResult of relayout/relayout2 with a full-screen
     * frame.  ViewRootImpl passes its own mTmpFrames in, so writing these fields is
     * what makes mWinFrameInScreen (and therefore the SurfaceControl/BLASTBufferQueue
     * size) non-zero.
     */

    /*
     * FLAG_DIM_BEHIND is normally implemented by WindowManagerService as a
     * full-screen color layer below the dimming window.  Create the same thing
     * with a color-only SurfaceControl; the composer blends it like any other
     * layer.
     */



    /*
     * droidlet's window session stands in for WindowManagerService.  The
     * client already owns its SurfaceControl, so apply the server-side parts
     * of a relayout here: position, layer order and window alpha.  Without
     * this, dialogs would share layer 0 with the activity and draw at (0,0).
     */



    /*
     * In-process content service.  There are no ContentProviders in this
     * process yet, so observer notifications are bookkeeping only; the
     * important part is that register/unregister are accepted instead of
     * dereferencing a null binder.
     */

    /*
     * InputManager shim.  KeyCharacterMap.load() needs an InputManagerGlobal
     * with a virtual keyboard InputDevice; the device's character map comes
     * from Virtual.kcm so key events produce ASCII text.
     */




    static Object parceledList(List<Object> list) {
        try {
            return Class.forName("android.content.pm.ParceledListSlice")
                    .getConstructor(List.class).newInstance(list);
        } catch (Throwable t) {
            return null;
        }
    }






    static Object emptyArray(String className) {
        try {
            Class<?> type = Class.forName(className);
            return java.lang.reflect.Array.newInstance(type, 0);
        } catch (Throwable t) {
            return new Object[0];
        }
    }

    // The host never suspends: the compositor keeps presenting and the process
    // keeps running, so power management is bookkeeping only.  Wake locks are
    // accepted and forgotten, and the device reports itself awake.



    static Object invokeNoThrow(Object target, String method) {
        try {
            return target.getClass().getMethod(method).invoke(target);
        } catch (Throwable t) {
            return null;
        }
    }

    // The host has no USB host controller.  Report an empty device list and
    // deny permissions; apps that probe for USB (SDL HID, gamepads) then fall
    // back to their normal input paths.

    static Object localProxy(String interfaceName, InvocationHandler handler)
            throws Exception {
        Class<?> iface = Class.forName(interfaceName);
        return java.lang.reflect.Proxy.newProxyInstance(
                iface.getClassLoader(), new Class<?>[] {iface}, handler);
    }













    /*
     * Notification service shim.  Toasts are accepted and logged instead of
     * being displayed; everything else is a successful no-op so that
     * NotificationManager and app startup code never see a null service.
     */


    /*
     * ContextImpl.getDisplayNoVerify() resolves the default display through
     * ResourcesManager.getAdjustedDisplay() -> DisplayManagerGlobal, which
     * returns null unless IDisplayManager reports a DisplayInfo.  PhoneWindow
     * dereferences the resulting Display in DecorContext.<init>, so provide a
     * synthetic 1280x800 @160dpi built-in display.
     */



    // ---------------------------------------------------------------------
    // Reflection helpers
    // ---------------------------------------------------------------------

    static Object defaultValue(Class<?> type) {
        if (!type.isPrimitive() || type == void.class) {
            return null;
        }
        if (type == boolean.class) {
            return false;
        }
        if (type == byte.class) {
            return (byte) 0;
        }
        if (type == char.class) {
            return (char) 0;
        }
        if (type == short.class) {
            return (short) 0;
        }
        if (type == int.class) {
            return 0;
        }
        if (type == long.class) {
            return 0L;
        }
        if (type == float.class) {
            return 0.0f;
        }
        if (type == double.class) {
            return 0.0d;
        }
        return null;
    }

    static int staticInt(Class<?> owner, String name, int fallback) {
        try {
            Field field = findField(owner, name);
            field.setAccessible(true);
            return field.getInt(null);
        } catch (Throwable t) {
            return fallback;
        }
    }

    static int callStaticInt(String className, String method) {
        try {
            Class<?> owner = Class.forName(className);
            return (Integer) owner.getMethod(method).invoke(null);
        } catch (Throwable t) {
            log("unable to call " + className + "." + method + ": " + t);
            return 1000;
        }
    }

    static void setField(Object target, String name, Object value)
            throws Exception {
        Field field = findField(target.getClass(), name);
        field.setAccessible(true);
        field.set(target, value);
    }

    static Object getFieldValue(Object target, String name) throws Exception {
        Field field = findField(target.getClass(), name);
        field.setAccessible(true);
        return field.get(target);
    }

    static Field findField(Class<?> owner, String name) throws NoSuchFieldException {
        for (Class<?> c = owner; c != null; c = c.getSuperclass()) {
            try {
                return c.getDeclaredField(name);
            } catch (NoSuchFieldException ignored) {
                // keep walking
            }
        }
        throw new NoSuchFieldException(name);
    }

    static void mkdirs(File dir) {
        if (!dir.exists() && !dir.mkdirs() && !dir.isDirectory()) {
            log("unable to create " + dir);
        }
    }

    static void deleteRecursively(File file) {
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) {
                deleteRecursively(child);
            }
        }
        if (file.exists() && !file.delete()) {
            log("unable to delete " + file);
        }
    }

    static void log(String message) {
        android.util.Log.i(TAG, message);
    }

    private static int countOpenFds() {
        File[] entries = new File("/proc/self/fd").listFiles();
        return entries != null ? entries.length : -1;
    }

    private static void logExitStats() {
        int fds = countOpenFds();
        log("exit: open fds=" + fds + " startup=" + sStartupFds);
        String fail = System.getenv("DROIDLET_FAIL_ON_FD_LEAK");
        if (fail != null && !fail.isEmpty() && sStartupFds >= 0) {
            long allowance = envLong("DROIDLET_FD_LEAK_ALLOWANCE", 64);
            if (fds > sStartupFds + allowance) {
                android.util.Log.e(TAG, "fd leak detected: startup=" + sStartupFds
                        + " exit=" + fds + " allowance=" + allowance);
                Runtime.getRuntime().halt(1);
            }
        }
    }

    // Implemented by libdroidlet_presenter.so (droidlet/droidlet/presenter/jni.cpp).
    static native Object nativeCreateClientInputChannel();
    static native int nativeLastInputChannelId();
    static native void nativeSetActiveInputChannel(int id);
    static native void nativeSetInputWindowOffset(float x, float y);
    static native void nativeSetInputWindowInset(float x, float y);
    static native void nativeSetActiveInputLayer(int layerId);
    static native Object nativeLoadKeyCharacterMap(String path);
    static native void nativeSetInputFocus(boolean focused);
    static native boolean nativeHostWaitFrame(int timeoutMs);
    static native void nativeHostRemoveLayer(int layerId);
    static native float[] nativeHostLayerPosition(int layerId);
    static native boolean nativeInputScriptFinished();
    static native int nativeHostFrameWidth();
    static native int nativeHostFrameHeight();
    static native long[] nativeHostFrameStats();
    // Copies the composed 0xAARRGGBB frame; returns the number of pixels copied.
    static native int nativeHostCopyFrame(int[] pixels);
}
