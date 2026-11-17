/*
 * Small framework service handlers for DroidletLauncher.
 *
 * These are the IServiceManager proxies for the Android system services an
 * app touches during startup: audio, jobs, alarms, clipboard, accounts,
 * power/thermal, USB, media sessions, locale, storage and input.  The big
 * stateful handlers live in DroidletLauncherActivityServices,
 * DroidletLauncherServices, DroidletLauncherBroadcasts and
 * DroidletLauncherWindows.
 */

package android.app;

import android.os.Binder;
import android.os.Bundle;

import java.io.File;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;

import static android.app.DroidletLauncher.*;
import static android.app.DroidletLauncherJobs.*;
import static android.app.DroidletLauncherStartup.*;

final class DroidletLauncherSystemServices {
    private DroidletLauncherSystemServices() {}

    static final class UserManagerHandler implements InvocationHandler {
        @Override
        public Object invoke(Object proxy, Method method, Object[] args) {
            if ("isUserUnlocked".equals(method.getName())
                    || "isUserUnlockingOrUnlocked".equals(method.getName())
                    || "isUserRunning".equals(method.getName())) {
                return true;
            }
            return defaultValue(method.getReturnType());
        }
    }

    static final class InputMethodManagerHandler implements InvocationHandler {
        @Override
        public Object invoke(Object proxy, Method method, Object[] args) {
            // addClient(), reportFullscreenMode(), etc. are all fire-and-forget
            // for a window that never shows an editable view.
            return defaultValue(method.getReturnType());
        }
    }

    static final class ContentServiceHandler implements InvocationHandler {
        private final ArrayList<Object> mObservers = new ArrayList<>();

        @Override
        public Object invoke(Object proxy, Method method, Object[] args) throws Throwable {
            String name = method.getName();
            if ("registerContentObserver".equals(name)) {
                // registerContentObserver(Uri, boolean, IContentObserver, int, int)
                if (args != null && args.length > 2 && args[2] != null) {
                    mObservers.add(args[2]);
                }
                return null;
            }
            if ("unregisterContentObserver".equals(name)) {
                if (args != null && args.length > 0 && args[0] != null) {
                    mObservers.remove(args[0]);
                }
                return null;
            }
            if ("notifyChange".equals(name)) {
                // No providers: local observers are never notified.
                return null;
            }
            if ("getCurrentSyncs".equals(name) || "getCurrentSyncsAsUser".equals(name)) {
                return new ArrayList<Object>();
            }
            if ("getMasterSyncAutomatically".equals(name)
                    || "getMasterSyncAutomaticallyAsUser".equals(name)) {
                return false;
            }
            if ("getSyncAutomatically".equals(name)
                    || "getSyncAutomaticallyAsUser".equals(name)) {
                return false;
            }
            if ("getIsSyncable".equals(name) || "getIsSyncableAsUser".equals(name)) {
                return 0;
            }
            return defaultValue(method.getReturnType());
        }
    }

    static final class PermissionManagerHandler implements InvocationHandler {
        @Override
        public Object invoke(Object proxy, Method method, Object[] args) {
            String name = method.getName();
            if ("getSplitPermissions".equals(name)) {
                return new ArrayList<Object>();
            }
            if ("getAllPermissionGroups".equals(name)
                    || "queryPermissionsByGroup".equals(name)) {
                return null;
            }
            return defaultValue(method.getReturnType());
        }
    }

    static final class ConnectivityManagerHandler implements InvocationHandler {
        private Object mNetwork;
        private Object mCapabilities;

        private synchronized Object network() throws Exception {
            if (mNetwork == null) {
                mNetwork = Class.forName("android.net.Network")
                        .getConstructor(int.class).newInstance(1);
            }
            return mNetwork;
        }

        private synchronized Object capabilities() throws Exception {
            if (mCapabilities == null) {
                Object caps = Class.forName("android.net.NetworkCapabilities")
                        .getDeclaredConstructor().newInstance();
                Class<?> capsClass = caps.getClass();
                Method add = capsClass.getMethod("addCapability", int.class);
                add.invoke(caps, staticInt(capsClass, "NET_CAPABILITY_INTERNET", 12));
                add.invoke(caps, staticInt(capsClass, "NET_CAPABILITY_NOT_RESTRICTED", 13));
                add.invoke(caps, staticInt(capsClass, "NET_CAPABILITY_TRUSTED", 14));
                add.invoke(caps, staticInt(capsClass, "NET_CAPABILITY_VALIDATED", 16));
                mCapabilities = caps;
            }
            return mCapabilities;
        }

        private Object networkRequest(Object[] args, boolean listen) {
            try {
                Class<?> capsClass = Class.forName("android.net.NetworkCapabilities");
                Object caps = null;
                if (args != null) {
                    for (Object arg : args) {
                        if (capsClass.isInstance(arg)) {
                            caps = arg;
                            break;
                        }
                    }
                }
                if (caps == null) {
                    caps = capabilities();
                }
                Class<?> requestClass = Class.forName("android.net.NetworkRequest");
                Class<?> typeClass = Class.forName("android.net.NetworkRequest$Type");
                Object type = enumConstant(typeClass, listen ? "LISTEN" : "REQUEST");
                return requestClass
                        .getConstructor(capsClass, int.class, int.class, typeClass)
                        .newInstance(caps, 0, -1, type);
            } catch (Throwable t) {
                log("connectivity: unable to create NetworkRequest: " + t);
                return null;
            }
        }

        @Override
        public Object invoke(Object proxy, Method method, Object[] args) throws Throwable {
            String name = method.getName();
            if ("getActiveNetwork".equals(name)) {
                return network();
            }
            if ("getNetworkCapabilities".equals(name)) {
                // A null Network is not accepted by AndroidX callers; a fake
                // internet-capable network keeps WorkManager's tracker happy.
                return capabilities();
            }
            if ("getActiveNetworkInfo".equals(name) || "getNetworkInfo".equals(name)
                    || "getLinkProperties".equals(name)) {
                return null;
            }
            if ("getAllNetworks".equals(name)) {
                Object array = java.lang.reflect.Array.newInstance(
                        Class.forName("android.net.Network"), 1);
                java.lang.reflect.Array.set(array, 0, network());
                return array;
            }
            if ("getAllNetworkInfo".equals(name)) {
                return null;
            }
            if ("isDefaultNetworkActive".equals(name)) {
                return true;
            }
            if ("isActiveNetworkMetered".equals(name)) {
                return false;
            }
            if ("getRestrictBackgroundStatus".equals(name)) {
                // RESTRICT_BACKGROUND_STATUS_DISABLED
                return 1;
            }
            if ("requestNetwork".equals(name) || "listenForNetwork".equals(name)
                    || "pendingRequestForNetwork".equals(name)) {
                // ConnectivityManager keeps the returned NetworkRequest on its
                // NetworkCallback and throws "NetworkCallback was not
                // registered" from unregisterNetworkCallback() when it is null
                // (Glide's connectivity monitor does exactly that on onStop).
                return networkRequest(args, "listenForNetwork".equals(name));
            }
            if ("registerNetworkCallback".equals(name)
                    || "registerDefaultNetworkCallback".equals(name)
                    || "unregisterNetworkCallback".equals(name)
                    || "releaseNetworkRequest".equals(name)
                    || "addDefaultNetworkActiveListener".equals(name)
                    || "removeDefaultNetworkActiveListener".equals(name)) {
                return null;
            }
            return defaultValue(method.getReturnType());
        }
    }

    static final class JobSchedulerHandler implements InvocationHandler {
        @Override
        public Object invoke(Object proxy, Method method, Object[] args) {
            String name = method.getName();
            if ("schedule".equals(name) || "enqueue".equals(name)
                    || "scheduleAsPackage".equals(name)) {
                try {
                    scheduleNewJob((String) args[0], args[1]);
                } catch (Throwable t) {
                    log("job: schedule failed: " + t);
                }
                // JobScheduler.RESULT_SUCCESS
                return 0;
            }
            if ("cancel".equals(name)) {
                cancelJob((Integer) args[1]);
                return null;
            }
            if ("cancelAll".equals(name)) {
                ArrayList<Integer> ids;
                synchronized (sJobLock) {
                    ids = new ArrayList<>(sJobs.keySet());
                }
                for (Integer id : ids) {
                    cancelJob(id);
                }
                return null;
            }
            if ("cancelAllInNamespace".equals(name)) {
                String namespace = (String) args[0];
                ArrayList<Integer> ids = new ArrayList<>();
                synchronized (sJobLock) {
                    for (JobRecord record : sJobs.values()) {
                        if (namespace == null || namespace.equals(record.namespace)) {
                            ids.add(record.jobId);
                        }
                    }
                }
                for (Integer id : ids) {
                    cancelJob(id);
                }
                return null;
            }
            if ("getAllPendingJobs".equals(name)) {
                return jobMap();
            }
            if ("getAllPendingJobsInNamespace".equals(name)) {
                String namespace = (String) args[0];
                ArrayList<Object> list = new ArrayList<>();
                synchronized (sJobLock) {
                    for (JobRecord record : sJobs.values()) {
                        if (namespace == null || namespace.equals(record.namespace)) {
                            list.add(record.info);
                        }
                    }
                }
                return parceledList(list);
            }
            if ("getPendingJob".equals(name)) {
                int jobId = (Integer) args[1];
                synchronized (sJobLock) {
                    JobRecord record = sJobs.get(jobId);
                    return record != null ? record.info : null;
                }
            }
            if ("getStartedJobs".equals(name)) {
                ArrayList<Object> list = new ArrayList<>();
                synchronized (sJobLock) {
                    for (JobRecord record : sRunningJobs.values()) {
                        list.add(record.info);
                    }
                }
                return list;
            }
            if ("getAllJobSnapshots".equals(name)) {
                return parceledList(new ArrayList<Object>());
            }
            if ("getPendingJobReasons".equals(name)) {
                return new int[0];
            }
            if ("getPendingJobReasonsHistory".equals(name)) {
                return new ArrayList<Object>();
            }
            if ("getPendingJobReasonStats".equals(name)) {
                return new java.util.HashMap<String, Object>();
            }
            if ("getPendingJobReason".equals(name)) {
                return 0;
            }
            if ("canRunUserInitiatedJobs".equals(name)
                    || "hasRunUserInitiatedJobsPermission".equals(name)) {
                return true;
            }
            return defaultValue(method.getReturnType());
        }
    }

    static final class AppOpsHandler implements InvocationHandler {
        @Override
        public Object invoke(Object proxy, Method method, Object[] args) {
            String name = method.getName();
            if (name.startsWith("checkOperation")) {
                // AppOpsManager.MODE_ALLOWED
                return 0;
            }
            if ("getPackagesForOps".equals(name) || "getOpsForPackage".equals(name)
                    || "getHistoricalOps".equals(name)) {
                return new ArrayList<Object>();
            }
            if ("getUidOps".equals(name)) {
                return new ArrayList<Object>();
            }
            return defaultValue(method.getReturnType());
        }
    }

    static final class ShortcutServiceHandler implements InvocationHandler {
        @Override
        public Object invoke(Object proxy, Method method, Object[] args) {
            String name = method.getName();
            if ("getShortcuts".equals(name) || "getPinnedShortcuts".equals(name)
                    || "getManifestShortcuts".equals(name)) {
                return parceledList(new ArrayList<Object>());
            }
            if ("getMaxShortcutCountPerActivity".equals(name)) {
                return 5;
            }
            if ("getIconMaxWidth".equals(name) || "getIconMaxHeight".equals(name)
                    || "getRemainingCallCount".equals(name)) {
                return 0;
            }
            if ("isRateLimitingActive".equals(name)
                    || "hasShortcutHostPermission".equals(name)
                    || "isRequestPinItemSupported".equals(name)) {
                return false;
            }
            return defaultValue(method.getReturnType());
        }
    }

    static final class RoleManagerHandler implements InvocationHandler {
        @Override
        public Object invoke(Object proxy, Method method, Object[] args) {
            String name = method.getName();
            if (name.startsWith("isRole") || "isRoleAvailableAsUser".equals(name)
                    || "isRoleHeldAsUser".equals(name)) {
                return false;
            }
            Class<?> returnType = method.getReturnType();
            if ("android.content.pm.ParceledListSlice".equals(returnType.getName())) {
                return parceledList(new ArrayList<Object>());
            }
            if (java.util.List.class.isAssignableFrom(returnType)) {
                return new ArrayList<Object>();
            }
            return defaultValue(returnType);
        }
    }

    static final class AlarmManagerHandler implements InvocationHandler {
        @Override
        public Object invoke(Object proxy, Method method, Object[] args) {
            String name = method.getName();
            if ("set".equals(name)) {
                handleSetAlarm((String) args[0], (Integer) args[1], (Long) args[2],
                        (Long) args[4], args[6], args[7], (String) args[8]);
                return null;
            }
            if ("remove".equals(name)) {
                handleRemoveAlarm(args[0], args[1]);
                return null;
            }
            if ("removeAll".equals(name)) {
                handleRemoveAllAlarms((String) args[0]);
                return null;
            }
            if ("canScheduleExactAlarms".equals(name)
                    || "hasScheduleExactAlarm".equals(name)) {
                return true;
            }
            if ("getNextAlarmClock".equals(name) || "getNextAlarmClockImpl".equals(name)) {
                return null;
            }
            return defaultValue(method.getReturnType());
        }
    }

    static final class ClipboardHandler implements InvocationHandler {
        private Object mClip;

        @Override
        public Object invoke(Object proxy, Method method, Object[] args) {
            String name = method.getName();
            if ("setPrimaryClip".equals(name)) {
                mClip = args != null && args.length > 0 ? args[0] : null;
                return null;
            }
            if ("getPrimaryClip".equals(name)) {
                return mClip;
            }
            if ("getPrimaryClipDescription".equals(name)) {
                return mClip == null ? null
                        : invokeNoThrow(mClip, "getDescription");
            }
            if ("hasPrimaryClip".equals(name) || "hasClipboardText".equals(name)) {
                return mClip != null;
            }
            if ("clearPrimaryClip".equals(name)) {
                mClip = null;
                return null;
            }
            return defaultValue(method.getReturnType());
        }
    }

    static final class AccountManagerHandler implements InvocationHandler {
        @Override
        public Object invoke(Object proxy, Method method, Object[] args) {
            String name = method.getName();
            if (name.startsWith("getAccounts")) {
                return emptyArray("android.accounts.Account");
            }
            if ("getAuthenticatorTypes".equals(name)
                    || "getAuthenticatorTypesAsUser".equals(name)) {
                return emptyArray("android.accounts.AuthenticatorDescription");
            }
            if ("getAccountsAndVisibilityForPackage".equals(name)) {
                return new java.util.HashMap<String, Integer>();
            }
            if ("hasFeatures".equals(name)) {
                return false;
            }
            return defaultValue(method.getReturnType());
        }
    }

    static final class PowerManagerHandler implements InvocationHandler {
        @Override
        public Object invoke(Object proxy, Method method, Object[] args) {
            String name = method.getName();
            if ("isWakeLockLevelSupported".equals(name)
                    || "isWakeLockLevelSupportedWithDisplayId".equals(name)
                    || "isInteractive".equals(name)
                    || "isDisplayInteractive".equals(name)) {
                return true;
            }
            if ("getBrightnessConstraint".equals(name)) {
                // PowerManager.BRIGHTNESS_MAX; the host display is not dimmed.
                return 1.0f;
            }
            if ("getPowerSaveState".equals(name)) {
                // android.os.PowerSaveState.Builder().build(); the framework
                // type is not on the overlay's compile classpath.
                try {
                    Class<?> builderClass = Class.forName("android.os.PowerSaveState$Builder");
                    Object builder = builderClass.getDeclaredConstructor().newInstance();
                    return builderClass.getMethod("build").invoke(builder);
                } catch (Throwable t) {
                    return null;
                }
            }
            // acquireWakeLock/releaseWakeLock/userActivity/wakeUp/goToSleep and
            // the battery-saver setters are no-ops.
            return defaultValue(method.getReturnType());
        }
    }

    static final class ThermalServiceHandler implements InvocationHandler {
        @Override
        public Object invoke(Object proxy, Method method, Object[] args) {
            String name = method.getName();
            if ("getCurrentTemperatures".equals(name)
                    || "getCurrentTemperaturesWithType".equals(name)
                    || "getCurrentCoolingDevices".equals(name)
                    || "getCurrentCoolingDevicesWithType".equals(name)) {
                return java.lang.reflect.Array.newInstance(
                        method.getReturnType().getComponentType(), 0);
            }
            if ("getThermalHeadroomThresholds".equals(name)) {
                return new float[0];
            }
            if ("getThermalHeadroom".equals(name)) {
                // ThermalService reports NaN when the forecast is unavailable.
                return Float.NaN;
            }
            if (name.startsWith("register") || name.startsWith("unregister")) {
                return true;
            }
            // getCurrentThermalStatus: Temperature.THROTTLING_NONE.
            return defaultValue(method.getReturnType());
        }
    }

    static final class VibratorHandler implements InvocationHandler {
        @Override
        public Object invoke(Object proxy, Method method, Object[] args) {
            String name = method.getName();
            if ("hasVibrator".equals(name) || "hasAmplitudeControl".equals(name)) {
                return true;
            }
            if ("getVibratorIds".equals(name)) {
                return new int[] {0};
            }
            return defaultValue(method.getReturnType());
        }
    }

    static final class UsbManagerHandler implements InvocationHandler {
        @Override
        public Object invoke(Object proxy, Method method, Object[] args) {
            String name = method.getName();
            if ("getDeviceList".equals(name)) {
                // out Bundle; leave it empty.
                return null;
            }
            if ("getCurrentAccessory".equals(name) || "openDevice".equals(name)
                    || "openAccessory".equals(name)
                    || "openAccessoryForInputStream".equals(name)
                    || "openAccessoryForOutputStream".equals(name)
                    || "getPortStatus".equals(name)) {
                return null;
            }
            if ("getPorts".equals(name)) {
                return new ArrayList<>();
            }
            if ("isAccessoryFfsEnabled".equals(name)) {
                return false;
            }
            Class<?> returnType = method.getReturnType();
            if (returnType.isArray()) {
                return java.lang.reflect.Array.newInstance(returnType.getComponentType(), 0);
            }
            return defaultValue(returnType);
        }
    }

    static final class MediaSessionManagerHandler implements InvocationHandler {
        @Override
        public Object invoke(Object proxy, Method method, Object[] args) throws Throwable {
            String name = method.getName();
            if ("createSession".equals(name)) {
                return localProxy("android.media.session.ISession", new MediaSessionHandler());
            }
            if ("getSessions".equals(name)) {
                return new ArrayList<Object>();
            }
            if ("isGlobalPriorityActive".equals(name)) {
                return false;
            }
            return defaultValue(method.getReturnType());
        }
    }

    static final class MediaSessionHandler implements InvocationHandler {
        @Override
        public Object invoke(Object proxy, Method method, Object[] args) throws Throwable {
            String name = method.getName();
            if ("getController".equals(name)) {
                return localProxy("android.media.session.ISessionController",
                                  new MediaSessionControllerHandler());
            }
            if ("getBinderForSetQueue".equals(name)) {
                return new Binder();
            }
            return defaultValue(method.getReturnType());
        }
    }

    static final class MediaSessionControllerHandler implements InvocationHandler {
        @Override
        public Object invoke(Object proxy, Method method, Object[] args) {
            String name = method.getName();
            if ("getPackageName".equals(name)) {
                return sPackageName;
            }
            if ("getTag".equals(name)) {
                return "droidlet";
            }
            return defaultValue(method.getReturnType());
        }
    }

    static final class BatteryStatsHandler implements InvocationHandler {
        @Override
        public Object invoke(Object proxy, Method method, Object[] args) {
            if ("isCharging".equals(method.getName())) {
                return false;
            }
            return defaultValue(method.getReturnType());
        }
    }

    static final class BatteryPropertiesHandler implements InvocationHandler {
        @Override
        public Object invoke(Object proxy, Method method, Object[] args) {
            if ("getProperty".equals(method.getName())) {
                // Non-zero means the property is unavailable; BatteryManager
                // turns that into Integer.MIN_VALUE (not 0/"battery absent").
                return 1;
            }
            return defaultValue(method.getReturnType());
        }
    }

    static final class LocaleManagerHandler implements InvocationHandler {
        @Override
        public Object invoke(Object proxy, Method method, Object[] args) {
            String name = method.getName();
            if ("getApplicationLocales".equals(name) || "getSystemLocales".equals(name)) {
                // LocaleList.getEmptyLocaleList() is not on the overlay's
                // compile classpath.
                try {
                    return Class.forName("android.os.LocaleList")
                            .getMethod("getEmptyLocaleList").invoke(null);
                } catch (Throwable t) {
                    return null;
                }
            }
            return defaultValue(method.getReturnType());
        }
    }

    static final class StorageManagerHandler implements InvocationHandler {
        @Override
        public Object invoke(Object proxy, Method method, Object[] args) {
            String name = method.getName();
            if ("mkdirs".equals(name) && args != null && args.length >= 2
                    && args[1] instanceof String) {
                // ContextImpl asks vold to create the app-private directory;
                // the host can simply create it.
                new File((String) args[1]).mkdirs();
                return null;
            }
            if ("getVolumeList".equals(name) || "getVolumes".equals(name)
                    || "getDisks".equals(name) || "getVolumeRecords".equals(name)) {
                Class<?> returnType = method.getReturnType();
                if (returnType.isArray()) {
                    return java.lang.reflect.Array.newInstance(returnType.getComponentType(), 0);
                }
            }
            // fixupAppDir, registerListener and the mount/format setters are
            // no-ops; there is no vold on the host.
            return defaultValue(method.getReturnType());
        }
    }

    static final class LegacyPermissionManagerHandler implements InvocationHandler {
        @Override
        public Object invoke(Object proxy, Method method, Object[] args) {
            if (method.getReturnType() == int.class) {
                // PackageManager.PERMISSION_DENIED; the host has no device
                // identifiers, telephony or carrier apps.
                return -1;
            }
            return defaultValue(method.getReturnType());
        }
    }

    static final class InputManagerHandler implements InvocationHandler {
        private Object mVirtualDevice;

        @Override
        public Object invoke(Object proxy, Method method, Object[] args) throws Throwable {
            String name = method.getName();
            if ("getInputDeviceIds".equals(name)) {
                return new int[] {0, -1};
            }
            if ("getInputDevice".equals(name) || "getInputDeviceByDescriptor".equals(name)) {
                return virtualDevice();
            }
            return defaultValue(method.getReturnType());
        }

        private Object virtualDevice() {
            if (mVirtualDevice != null) {
                return mVirtualDevice;
            }
            try {
                Class<?> deviceClass = Class.forName("android.view.InputDevice");
                Class<?> builderClass = Class.forName("android.view.InputDevice$Builder");
                Class<?> kcmClass = Class.forName("android.view.KeyCharacterMap");
                Object builder = builderClass.getDeclaredConstructor().newInstance();
                int sources = staticInt(deviceClass, "SOURCE_KEYBOARD", 0x101)
                        | staticInt(deviceClass, "SOURCE_DPAD", 0x201);
                builderClass.getMethod("setId", int.class).invoke(builder, 0);
                builderClass.getMethod("setName", String.class)
                        .invoke(builder, "droidlet virtual keyboard");
                builderClass.getMethod("setDescriptor", String.class)
                        .invoke(builder, "droidlet-virtual-keyboard");
                builderClass.getMethod("setIsVirtualDevice", boolean.class)
                        .invoke(builder, true);
                builderClass.getMethod("setSources", int.class).invoke(builder, sources);
                builderClass.getMethod("setKeyboardType", int.class).invoke(builder, 1);
                Object kcm = null;
                String root = System.getenv("ANDROID_ROOT");
                if (root == null || root.isEmpty()) {
                    root = "/system";
                }
                kcm = nativeLoadKeyCharacterMap(root + "/etc/keychars/Virtual.kcm");
                if (kcm == null) {
                    kcm = kcmClass.getMethod("obtainEmptyMap", int.class).invoke(null, 0);
                }
                builderClass.getMethod("setKeyCharacterMap", kcmClass)
                        .invoke(builder, kcm);
                mVirtualDevice = builderClass.getMethod("build").invoke(builder);
                log("input: created virtual keyboard device");
                return mVirtualDevice;
            } catch (Throwable t) {
                log("input: unable to create virtual device: " + t);
                t.printStackTrace(System.err);
                return null;
            }
        }
    }

    static final class AudioManagerHandler implements InvocationHandler {
        @Override
        public Object invoke(Object proxy, Method method, Object[] args) {
            String name = method.getName();
            if (name.startsWith("is") && method.getReturnType() == boolean.class) {
                return false;
            }
            if ("areNavigationRepeatSoundEffectsEnabled".equals(name)) {
                return false;
            }
            if ("getMode".equals(name)) {
                // AudioManager.MODE_NORMAL.
                return 0;
            }
            if ("getStreamVolume".equals(name) || "getStreamMaxVolume".equals(name)) {
                return 1;
            }
            Class<?> returnType = method.getReturnType();
            if (List.class.isAssignableFrom(returnType)) {
                // AudioManager.getAudioDevicesForAttributes() iterates the
                // service result unconditionally; an empty list means "no
                // routed device information" and lets clients fall back.
                return new ArrayList<>();
            }
            if (returnType.isArray()) {
                return java.lang.reflect.Array.newInstance(returnType.getComponentType(), 0);
            }
            return defaultValue(returnType);
        }
    }

    static final class AppWidgetManagerHandler implements InvocationHandler {
        @Override
        public Object invoke(Object proxy, Method method, Object[] args) {
            String name = method.getName();
            if ("getAppWidgetIdsForHost".equals(name) || "getAppWidgetIds".equals(name)) {
                return new int[0];
            }
            if ("isRequestPinAppWidgetSupported".equals(name)
                    || "requestPinAppWidget".equals(name)
                    || "hasBindAppWidgetPermission".equals(name)
                    || "isBoundWidgetPackage".equals(name)
                    || "setWidgetPreview".equals(name)
                    || "isFirstConfigActivityPending".equals(name)) {
                return false;
            }
            if ("getMaxBitmapMemory".equals(name)) {
                // 4 MiB, roughly what a small host surface needs.
                return 4 * 1024 * 1024;
            }
            return defaultValue(method.getReturnType());
        }
    }

    static final class UiModeManagerHandler implements InvocationHandler {
        @Override
        public Object invoke(Object proxy, Method method, Object[] args) {
            String name = method.getName();
            if ("getCurrentModeType".equals(name)) {
                // Configuration.UI_MODE_TYPE_NORMAL.
                return 1;
            }
            if ("getNightMode".equals(name) || "getApplicationNightMode".equals(name)) {
                // UiModeManager.MODE_NIGHT_AUTO.
                return 0;
            }
            if ("getNightModeCustomType".equals(name)) {
                // UiModeManager.MODE_NIGHT_CUSTOM_TYPE_UNKNOWN.
                return -1;
            }
            if ("getContrast".equals(name)) {
                return 0.0f;
            }
            return defaultValue(method.getReturnType());
        }
    }

    static final class NotificationManagerHandler implements InvocationHandler {
        @Override
        public Object invoke(Object proxy, Method method, Object[] args) throws Throwable {
            String name = method.getName();
            if (name.startsWith("enqueue") && method.getReturnType() == boolean.class) {
                if ("enqueueTextToast".equals(name) && args != null && args.length > 2) {
                    log("toast: " + args[2]);
                } else if ("enqueueToast".equals(name)) {
                    log("toast queued");
                }
                return true;
            }
            if ("areNotificationsEnabledForPackage".equals(name)
                    || "areNotificationsEnabled".equals(name)) {
                return true;
            }
            if ("getImportance".equals(name)) {
                // NotificationManager.IMPORTANCE_DEFAULT.
                return 3;
            }
            if ("android.content.pm.ParceledListSlice"
                    .equals(method.getReturnType().getName())) {
                return parceledList(new ArrayList<Object>());
            }
            return defaultValue(method.getReturnType());
        }
    }

}
