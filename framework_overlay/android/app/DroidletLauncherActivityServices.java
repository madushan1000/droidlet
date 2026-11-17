/*
 * The activity, package and task dispatchers for DroidletLauncher.
 *
 * One app runs per process, so these proxies answer for the local process:
 * ActivityManagerHandler turns activity/service/broadcast/pending-intent
 * transactions into calls on the local state machine, PackageManagerHandler
 * answers from the parsed manifest, and the task/controller handlers expose
 * the single top activity.
 */

package android.app;

import android.os.Bundle;
import android.os.IBinder;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;

import static android.app.DroidletLauncher.*;
import static android.app.DroidletLauncherServices.*;
import static android.app.DroidletLauncherStartup.*;
import static android.app.DroidletLauncherActivities.*;
import static android.app.DroidletLauncherBroadcasts.*;
import static android.app.DroidletLauncherInstall.*;

final class DroidletLauncherActivityServices {
    private DroidletLauncherActivityServices() {}

    static final class ActivityManagerHandler implements InvocationHandler {
        @Override
        public Object invoke(Object proxy, Method method, Object[] args) throws Throwable {
            String name = method.getName();
            if ("attachApplication".equals(name)) {
                onAttachApplication(args[0]);
                return null;
            }
            if ("startService".equals(name)) {
                return handleStartService(args[1], (Boolean) args[3]);
            }
            if ("stopService".equals(name)) {
                return handleStopService(args[1]);
            }
            if ("stopServiceToken".equals(name)) {
                return handleStopServiceToken(args[0], args[1], (Integer) args[2]);
            }
            if ("bindService".equals(name) || "bindServiceInstance".equals(name)) {
                String instanceName = "bindServiceInstance".equals(name)
                        ? (String) args[6] : null;
                int bindFlags = ((Number) args[5]).intValue();
                return handleBindService(args[2], args[4], bindFlags, instanceName);
            }
            if ("unbindService".equals(name)) {
                return handleUnbindService(args[0]);
            }
            if ("publishService".equals(name)) {
                handlePublishService(args[0], args[1], args[2]);
                return null;
            }
            if ("unbindFinished".equals(name)) {
                log("service: unbind finished");
                return null;
            }
            if ("serviceDoneExecuting".equals(name)) {
                handleServiceDoneExecuting(args[0], (Integer) args[1], (Integer) args[2],
                        (Integer) args[3]);
                return null;
            }
            if ("setServiceForeground".equals(name)) {
                handleSetServiceForeground(args[0], args[1], (Integer) args[2], args[3]);
                return null;
            }
            if ("registerReceiver".equals(name) || "registerReceiverWithFeature".equals(name)) {
                boolean feature = "registerReceiverWithFeature".equals(name);
                Object receiver = args[feature ? 4 : 2];
                Object filter = args[feature ? 5 : 3];
                String packageName = (String) args[1];
                String permission = (String) args[feature ? 6 : 4];
                int userId = (Integer) args[feature ? 7 : 5];
                int flags = (Integer) args[feature ? 8 : 6];
                registerReceiverRecord(receiver, filter, packageName, permission, userId, flags);
                return findStickyBroadcast(filter);
            }
            if ("unregisterReceiver".equals(name)) {
                unregisterReceiverRecord(args[0]);
                return null;
            }
            if ("getRegisteredIntentFilters".equals(name)) {
                ArrayList<Object> filters = new ArrayList<>();
                synchronized (sBroadcastLock) {
                    for (RegisteredReceiver record : sRegisteredReceivers) {
                        if (record.receiver == args[0] && record.filter != null) {
                            filters.add(record.filter);
                        }
                    }
                }
                return filters;
            }
            if ("broadcastIntent".equals(name) || "broadcastIntentWithFeature".equals(name)) {
                boolean feature = "broadcastIntentWithFeature".equals(name);
                Object intent = args[feature ? 2 : 1];
                Object resultTo = args[feature ? 4 : 3];
                int resultCode = (Integer) args[feature ? 5 : 4];
                String resultData = (String) args[feature ? 6 : 5];
                Object map = args[feature ? 7 : 6];
                String[] excludePackages = feature ? (String[]) args[10] : null;
                boolean serialized = (Boolean) args[feature ? 13 : 10];
                boolean sticky = (Boolean) args[feature ? 14 : 11];
                int userId = (Integer) args[feature ? 15 : 12];
                return handleBroadcastIntent(intent, resultTo, resultCode, resultData, map,
                        serialized, sticky, excludePackages, userId);
            }
            if ("unbroadcastIntent".equals(name)) {
                Object intent = args[1];
                synchronized (sBroadcastLock) {
                    for (int i = sStickyBroadcasts.size() - 1; i >= 0; i--) {
                        if (intentEquals(sStickyBroadcasts.get(i), intent)) {
                            sStickyBroadcasts.remove(i);
                        }
                    }
                }
                return null;
            }
            if ("finishReceiver".equals(name)) {
                handleFinishReceiver(args[0], (Integer) args[1], (String) args[2], args[3],
                        (Boolean) args[4]);
                return null;
            }
            if ("getIntentSender".equals(name) || "getIntentSenderWithFeature".equals(name)) {
                boolean feature = "getIntentSenderWithFeature".equals(name);
                int type = (Integer) args[0];
                String packageName = (String) args[1];
                Object token = args[feature ? 3 : 2];
                String resultWho = (String) args[feature ? 4 : 3];
                int requestCode = (Integer) args[feature ? 5 : 4];
                Object[] intents = (Object[]) args[feature ? 6 : 5];
                String[] resolvedTypes = (String[]) args[feature ? 7 : 6];
                int flags = (Integer) args[feature ? 8 : 7];
                Object options = args[feature ? 9 : 8];
                int userId = (Integer) args[feature ? 10 : 9];
                return handleGetIntentSender(type, packageName, token, resultWho, requestCode,
                        intents, resolvedTypes, flags, options, userId);
            }
            if ("sendIntentSender".equals(name)) {
                Object target = args[1];
                PendingIntentRecord record;
                synchronized (sPendingIntentLock) {
                    record = sPendingIntents.get(target);
                }
                if (record != null) {
                    record.send((Integer) args[3], args[4], args[6]);
                }
                return 0;
            }
            if ("cancelIntentSender".equals(name)) {
                cancelIntentSender(args[0]);
                return null;
            }
            if ("getForegroundServiceType".equals(name)) {
                return 0;
            }
            if ("peekService".equals(name)) {
                return handlePeekService(args[0]);
            }
            if ("updateServiceGroup".equals(name)) {
                return null;
            }
            if ("getRunningServiceConnections".equals(name)) {
                return new ArrayList<Object>();
            }
            if ("getCurrentUserId".equals(name) || "getCurrentUser".equals(name)) {
                return 0;
            }
            if ("isUserRunning".equals(name)) {
                return true;
            }
            if ("getHistoricalProcessExitReasons".equals(name)
                    || "getHistoricalProcessStartReasons".equals(name)
                    || "getRecentTasks".equals(name)) {
                // WorkManager's ForceStopRunnable iterates the exit-reason list
                // and assumes it is non-null.
                return parceledList(new ArrayList<Object>());
            }
            if ("getRunningTasks".equals(name)
                    || "getRunningAppProcesses".equals(name)
                    || "getRunningServices".equals(name)
                    || "getServices".equals(name)
                    || "getAppTasks".equals(name)) {
                return new ArrayList<Object>();
            }
            if ("finishActivity".equals(name)) {
                final Object token = args[0];
                final int resultCode = (Integer) args[1];
                final Object data = args[2];
                postToActivityThread(new Runnable() {
                    @Override
                    public void run() {
                        try {
                            handleFinishActivity(token, resultCode, data, false);
                        } catch (Throwable t) {
                            log("finishActivity failed: " + t);
                        }
                    }
                });
                return true;
            }
            return defaultValue(method.getReturnType());
        }
    }

    static final class PackageManagerHandler implements InvocationHandler {
        @Override
        public Object invoke(Object proxy, Method method, Object[] args) throws Throwable {
            String name = method.getName();
            if ("getApplicationInfo".equals(name)) {
                String pkg = (String) args[0];
                if (sPackageName.equals(pkg)) {
                    return sAppInfo;
                }
                return null;
            }
            if ("getPackageInfo".equals(name) || "getPackageInfoVersioned".equals(name)) {
                String pkg = (String) args[0];
                if ("getPackageInfoVersioned".equals(name)) {
                    pkg = componentField(args[0], "getPackageName");
                }
                if (sPackageName.equals(pkg)) {
                    long flags = args.length > 1 ? (Long) args[1] : 0L;
                    return makePackageInfo((int) flags);
                }
                if ("android".equals(pkg)) {
                    // NewPipe's OkHttp dispatcher asks for the framework
                    // package's signature ("PackageValidator"); returning
                    // null makes ApplicationPackageManager throw
                    // NameNotFoundException and kills the player service.
                    long flags = args.length > 1 ? (Long) args[1] : 0L;
                    return makeFrameworkPackageInfo((int) flags);
                }
                return null;
            }
            if ("getActivityInfo".equals(name)) {
                String pkg = componentField(args[0], "getPackageName");
                String componentClass = componentField(args[0], "getClassName");
                if (sPackageName.equals(pkg)) {
                    long flags = args.length > 1 ? (Long) args[1] : 0L;
                    Object parsed = findParsedActivityComponent(sParsedPackage, componentClass);
                    if (parsed != null) {
                        // NativeActivity reads android.app.lib_name from
                        // ActivityInfo.metaData, so generate the public info
                        // with the requested flags instead of returning the
                        // raw parsed component.
                        Object generated = generateComponentInfo("Activity", parsed, (int) flags);
                        Object metadata = generated == null ? null
                                : getFieldValue(generated, "metaData");
                        if (metadata instanceof Bundle) {
                            String libName = ((Bundle) metadata).getString("android.app.lib_name");
                            if (libName != null) {
                                log("activity " + componentClass + " lib_name=" + libName);
                            }
                        }
                        return generated;
                    }
                    if (sClassName.equals(componentClass)) {
                        return sActivityInfo;
                    }
                }
                return null;
            }
            if ("getServiceInfo".equals(name) || "getProviderInfo".equals(name)
                    || "getReceiverInfo".equals(name)) {
                String pkg = componentField(args[0], "getPackageName");
                String componentClass = componentField(args[0], "getClassName");
                if (!sPackageName.equals(pkg)) {
                    return null;
                }
                long flags = args.length > 1 ? (Long) args[1] : 0L;
                if ("getServiceInfo".equals(name)) {
                    return generateComponentInfo("Service",
                            findParsedService(sParsedPackage, componentClass), (int) flags);
                }
                if ("getProviderInfo".equals(name)) {
                    return generateComponentInfo("Provider",
                            findParsedProvider(sParsedPackage, componentClass), (int) flags);
                }
                return generateComponentInfo("Activity",
                        findParsedReceiver(sParsedPackage, componentClass), (int) flags);
            }
            if ("resolveContentProvider".equals(name)
                    || "resolveContentProviderForUid".equals(name)) {
                String authority = (String) args[0];
                long flags = args.length > 1 ? (Long) args[1] : 0L;
                return generateComponentInfo("Provider",
                        findParsedProviderByAuthority(sParsedPackage, authority), (int) flags);
            }
            if ("queryContentProviders".equals(name)) {
                long flags = (Long) args[2];
                ArrayList<Object> list = new ArrayList<>();
                List<?> providers = (List<?>) getFieldValue(sParsedPackage, "providers");
                for (Object provider : providers) {
                    Object info = generateComponentInfo("Provider", provider, (int) flags);
                    if (info != null) {
                        list.add(info);
                    }
                }
                return Class.forName("android.content.pm.ParceledListSlice")
                        .getConstructor(List.class).newInstance(list);
            }
            if ("getInstalledPackages".equals(name)) {
                long flags = (Long) args[0];
                ArrayList<Object> list = new ArrayList<>();
                list.add(makePackageInfo((int) flags));
                return Class.forName("android.content.pm.PackageInfoList")
                        .getConstructor(List.class).newInstance(list);
            }
            if ("getInstalledApplications".equals(name)) {
                long flags = (Long) args[0];
                ArrayList<Object> list = new ArrayList<>();
                list.add(sAppInfo);
                return Class.forName("android.content.pm.ParceledListSlice")
                        .getConstructor(List.class).newInstance(list);
            }
            if ("queryIntentActivities".equals(name)
                    || "queryIntentServices".equals(name)
                    || "queryIntentReceivers".equals(name)
                    || "queryBroadcastReceivers".equals(name)
                    || "queryIntentContentProviders".equals(name)) {
                return queryIntents(name, args[0]);
            }
            if ("queryIntentActivityOptions".equals(name)) {
                return parceledList(new ArrayList<Object>());
            }
            if ("resolveIntent".equals(name) || "resolveService".equals(name)) {
                Object slice = queryIntents(
                        "resolveIntent".equals(name)
                                ? "queryIntentActivities" : "queryIntentServices",
                        args[0]);
                List<?> list = (List<?>) slice.getClass().getMethod("getList").invoke(slice);
                return list.isEmpty() ? null : list.get(0);
            }
            if ("getPackagesForUid".equals(name)) {
                int uid = (Integer) args[0];
                if (uid == sUid || uid == 1000 || uid == 0) {
                    return new String[] {sPackageName};
                }
                return null;
            }
            if ("getNamesForUids".equals(name)) {
                return new String[] {sPackageName};
            }
            if ("getTargetSdkVersion".equals(name)) {
                return sTargetSdk;
            }
            if ("checkPermission".equals(name)
                    || "checkUidPermission".equals(name)
                    || "checkSignatures".equals(name)) {
                return 0; // PERMISSION_GRANTED
            }
            if ("hasSystemFeature".equals(name)) {
                return hasSystemFeature((String) args[0]);
            }
            if ("isPackageAvailable".equals(name)) {
                String pkg = (String) args[0];
                return sPackageName.equals(pkg);
            }
            if ("getPackageInstaller".equals(name)) {
                return packageInstaller();
            }
            if ("getInstallSourceInfo".equals(name)) {
                String pkg = (String) args[0];
                if (!sPackageName.equals(pkg)) {
                    return null;
                }
                try {
                    Class<?> infoClass = Class.forName("android.content.pm.InstallSourceInfo");
                    Class<?> signingClass = Class.forName("android.content.pm.SigningInfo");
                    return infoClass.getConstructor(String.class, signingClass,
                            String.class, String.class)
                            .newInstance(null, null, null, null);
                } catch (Throwable t) {
                    log("unable to build InstallSourceInfo: " + t);
                    return null;
                }
            }
            return defaultValue(method.getReturnType());
        }

        private Object queryIntents(String method, Object intent) throws Exception {
            boolean services = "queryIntentServices".equals(method)
                    || "resolveService".equals(method);
            boolean receivers = "queryIntentReceivers".equals(method)
                    || "queryBroadcastReceivers".equals(method);
            boolean providers = "queryIntentContentProviders".equals(method);
            String componentKey = services ? "serviceInfo"
                    : (providers ? "providerInfo" : "activityInfo");
            String listField = services ? "services"
                    : (receivers ? "receivers" : (providers ? "providers" : "activities"));
            ArrayList<Object> results = new ArrayList<>();

            String explicitPkg = null;
            String explicitClass = null;
            Object component = intent.getClass().getMethod("getComponent").invoke(intent);
            if (component != null) {
                explicitPkg = componentField(component, "getPackageName");
                explicitClass = componentField(component, "getClassName");
            }

            List<?> items = (List<?>) getFieldValue(sParsedPackage, listField);
            for (Object item : items) {
                Object info = getFieldValue(item, "info");
                String className = (String) getFieldValue(info, "name");
                if (explicitClass != null) {
                    if (sPackageName.equals(explicitPkg) && explicitClass.equals(className)) {
                        results.add(makeResolveInfo(info, null, componentKey));
                    }
                    continue;
                }
                List<?> filters = (List<?>) getFieldValue(item, "intents");
                for (Object filter : filters) {
                    if (intentFilterMatches(filter, intent)) {
                        results.add(makeResolveInfo(info, filter, componentKey));
                        break;
                    }
                }
            }
            return Class.forName("android.content.pm.ParceledListSlice")
                    .getConstructor(List.class).newInstance(results);
        }

        private Object makeFrameworkPackageInfo(int flags) throws Exception {
            Class<?> infoClass = Class.forName("android.content.pm.PackageInfo");
            Object info = infoClass.getDeclaredConstructor().newInstance();
            setField(info, "packageName", "android");
            int sdk = 0;
            String release = "";
            try {
                Class<?> versionClass = Class.forName("android.os.Build$VERSION");
                sdk = versionClass.getField("SDK_INT").getInt(null);
                release = (String) versionClass.getField("RELEASE").get(null);
            } catch (Throwable ignored) {
            }
            setField(info, "versionName", release);
            setField(info, "versionCode", sdk);

            Class<?> appInfoClass = Class.forName("android.content.pm.ApplicationInfo");
            Object appInfo = appInfoClass.getDeclaredConstructor().newInstance();
            setField(appInfo, "packageName", "android");
            setField(appInfo, "uid", 1000); // Process.SYSTEM_UID
            setField(appInfo, "flags", 1);  // ApplicationInfo.FLAG_SYSTEM
            setField(appInfo, "enabled", true);
            setField(appInfo, "targetSdkVersion", sdk);
            setField(info, "applicationInfo", appInfo);
            // NewPipe's PackageValidator (from UAMP) computes the platform
            // signature with PackageInfo.signatures[0] and errors out when it
            // is missing.  The host has no platform key, so use a fixed
            // certificate blob; only its SHA-256 is used, for comparison with
            // callers that are signed like the platform.
            try {
                Class<?> signatureClass = Class.forName("android.content.pm.Signature");
                Object signature = signatureClass.getConstructor(byte[].class)
                        .newInstance((Object) new byte[] {
                                0x30, 0x2a, 0x30, 0x08, 0x06, 0x03, 0x55, 0x04,
                                0x03, 0x0c, 0x01, 0x64, 0x03, 0x1e, 0x00, 0x30,
                                0x1b, 0x06, 0x03, 0x55, 0x04, 0x03, 0x0c, 0x14,
                                0x64, 0x72, 0x6f, 0x69, 0x64, 0x6c, 0x65, 0x74,
                                0x20, 0x70, 0x6c, 0x61, 0x74, 0x66, 0x6f, 0x72,
                                0x6d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                        });
                Object signatures = java.lang.reflect.Array.newInstance(signatureClass, 1);
                java.lang.reflect.Array.set(signatures, 0, signature);
                setField(info, "signatures", signatures);
            } catch (Throwable ignored) {
            }
            return info;
        }

        private Object makePackageInfo(int flags) throws Exception {
            if (sParsedPackage != null) {
                try {
                    Class<?> parserClass = Class.forName("android.content.pm.PackageParser");
                    Class<?> pkgClass = Class.forName("android.content.pm.PackageParser$Package");
                    Class<?> stateClass =
                            Class.forName("android.content.pm.pkg.FrameworkPackageUserState");
                    Object state = stateClass.getField("DEFAULT").get(null);
                    Method generate = parserClass.getMethod("generatePackageInfo",
                            pkgClass, int[].class, int.class, long.class, long.class,
                            Set.class, stateClass, int.class);
                    // PackageManagerService always evaluates components with an
                    // explicit direct-boot match; without one PackageParser.isMatch()
                    // rejects every activity, service and provider.
                    int generateFlags = flags | MATCH_DIRECT_BOOT_AWARE
                            | MATCH_DIRECT_BOOT_UNAWARE | MATCH_DISABLED_COMPONENTS;
                    Object info = generate.invoke(null, sParsedPackage, new int[0], generateFlags,
                            0L, 0L, java.util.Collections.emptySet(), state, 0);
                    if (info != null) {
                        return info;
                    }
                } catch (Throwable t) {
                    log("generatePackageInfo failed: " + t);
                }
            }
            Class<?> infoClass = Class.forName("android.content.pm.PackageInfo");
            Object info = infoClass.getDeclaredConstructor().newInstance();
            setField(info, "packageName", sPackageName);
            setField(info, "applicationInfo", sAppInfo);
            if (sParsedPackage != null) {
                setField(info, "versionName", getFieldValue(sParsedPackage, "mVersionName"));
                setField(info, "versionCode", getFieldValue(sParsedPackage, "mVersionCode"));
                setField(info, "versionCodeMajor",
                        getFieldValue(sParsedPackage, "mVersionCodeMajor"));
            } else {
                setField(info, "versionName", "1.0");
                setField(info, "versionCode", 1);
                setField(info, "versionCodeMajor", 0);
            }
            return info;
        }
    }

    static final class PackageInstallerHandler implements InvocationHandler {
        @Override
        public Object invoke(Object proxy, Method method, Object[] args) {
            String name = method.getName();
            if ("getAllSessions".equals(name)
                    || "getMySessions".equals(name)
                    || "getStagedSessions".equals(name)) {
                return parceledList(new ArrayList<Object>());
            }
            if ("getSessionInfo".equals(name) || "openSession".equals(name)) {
                return null;
            }
            return defaultValue(method.getReturnType());
        }
    }

    static final class ActivityTaskManagerHandler implements InvocationHandler {
        @Override
        public Object invoke(Object proxy, Method method, Object[] args) throws Throwable {
            String name = method.getName();
            if ("getActivityClientController".equals(name)) {
                return sActivityClientController;
            }
            if ("getAppTasks".equals(name)) {
                // ActivityManager.getAppTasks() calls size() on the result
                // without a null check; the Bridge library uses it during
                // Activity.onCreate.
                return new ArrayList<IBinder>();
            }
            if ("startActivity".equals(name) || "startActivityAsUser".equals(name)) {
                final Object intent = args[3];
                final Object resultTo = args[5];
                final String resultWho = (String) args[6];
                final int requestCode = (Integer) args[7];
                postToActivityThread(new Runnable() {
                    @Override
                    public void run() {
                        handleStartActivity(intent, resultTo, resultWho, requestCode);
                    }
                });
                return 0; // ActivityManager.START_SUCCESS
            }
            if ("startActivities".equals(name)) {
                final Object[] intents = (Object[]) args[3];
                final Object resultTo = args[6];
                postToActivityThread(new Runnable() {
                    @Override
                    public void run() {
                        if (intents != null) {
                            for (Object intent : intents) {
                                handleStartActivity(intent, resultTo, null, -1);
                            }
                        }
                    }
                });
                return 0;
            }
            return defaultValue(method.getReturnType());
        }
    }

    static final class ActivityClientControllerHandler
            implements InvocationHandler {
        @Override
        public Object invoke(Object proxy, Method method, Object[] args) throws Throwable {
            String name = method.getName();
            if ("getDisplayId".equals(name)) {
                return sDisplayId;
            }
            if ("getTaskForActivity".equals(name)) {
                return 1;
            }
            if ("willActivityBeVisible".equals(name)) {
                return true;
            }
            if ("onBackPressed".equals(name)) {
                final Object token = args[0];
                final Object callback = args[1];
                if (callback != null) {
                    try {
                        callback.getClass().getMethod("requestFinish").invoke(callback);
                        return null;
                    } catch (Throwable t) {
                        log("onBackPressed callback failed: " + t);
                    }
                }
                postToActivityThread(new Runnable() {
                    @Override
                    public void run() {
                        try {
                            handleFinishActivity(token, 0, null, false);
                        } catch (Throwable t) {
                            log("back finish failed: " + t);
                        }
                    }
                });
                return null;
            }
            if ("finishActivity".equals(name)) {
                final Object token = args[0];
                final int resultCode = (Integer) args[1];
                final Object data = args[2];
                final int finishTask = (Integer) args[3];
                postToActivityThread(new Runnable() {
                    @Override
                    public void run() {
                        try {
                            handleFinishActivity(token, resultCode, data, finishTask != 0);
                        } catch (Throwable t) {
                            log("finishActivity failed: " + t);
                        }
                    }
                });
                return true;
            }
            if ("finishActivityAffinity".equals(name)) {
                postToActivityThread(new Runnable() {
                    @Override
                    public void run() {
                        try {
                            finishAllActivities(0, null);
                        } catch (Throwable t) {
                            log("finishActivityAffinity failed: " + t);
                        }
                    }
                });
                return true;
            }
            return defaultValue(method.getReturnType());
        }
    }

}
