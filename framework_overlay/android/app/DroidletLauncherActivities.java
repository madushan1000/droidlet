/*
 * Activity task stack and lifecycle transactions for DroidletLauncher.
 *
 * One activity is launched at a time, but the launcher still keeps a small
 * task stack so startActivity/finish/result work: each ActivityRecord owns the
 * client token, intent and lifecycle state, and lifecycle changes are sent as
 * real ClientTransaction items to the app thread.
 */

package android.app;

import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;

import static android.app.DroidletLauncher.*;
import static android.app.DroidletLauncherInstall.*;
import static android.app.DroidletLauncherStartup.*;
import static android.app.DroidletLauncherWindows.*;

final class DroidletLauncherActivities {
    private DroidletLauncherActivities() {}

    static final class ActivityRecord {
        Object token;
        Object intent;
        Object info;
        Object resultTo;
        Object state;
        String className;
        String resultWho;
        int requestCode;
        int inputChannelId;
        boolean launched;
        boolean finished;
        boolean resumed;
        boolean stopped;
    }

    static ActivityRecord topRecord() {
        for (int i = sTaskStack.size() - 1; i >= 0; i--) {
            ActivityRecord record = sTaskStack.get(i);
            if (!record.finished) {
                return record;
            }
        }
        return null;
    }

    static ActivityRecord findRecordByClassName(String className) {
        for (int i = sTaskStack.size() - 1; i >= 0; i--) {
            ActivityRecord record = sTaskStack.get(i);
            if (!record.finished && record.className.equals(className)) {
                return record;
            }
        }
        return null;
    }

    static ActivityRecord createInitialRecord() throws Exception {
        ActivityRecord record = new ActivityRecord();
        record.token = newBinder();
        Object intent = makeIntent();
        Object info = sActivityInfo;
        String className = sClassName;
        if (System.getenv("DROIDLET_START_URI") != null) {
            Object resolved = resolveActivityInfo(intent);
            if (resolved != null) {
                info = resolved;
                className = (String) getFieldValue(resolved, "name");
                log("launch intent resolves to " + className);
            } else {
                log("no activity matches the start URI; using the main activity");
                intent.getClass()
                        .getMethod("setClassName", String.class, String.class)
                        .invoke(intent, sPackageName, sClassName);
            }
        }
        record.intent = intent;
        record.info = info;
        record.className = className;
        record.inputChannelId = -1;
        sActivityToken = record.token;
        sTaskStack.add(record);
        return record;
    }

    static Object resolveActivityInfo(Object intent) throws Exception {
        if (intent == null) {
            return null;
        }
        Class<?> intentClass = intent.getClass();
        Object component = intentClass.getMethod("getComponent").invoke(intent);
        if (component != null) {
            String pkg = componentField(component, "getPackageName");
            String cls = componentField(component, "getClassName");
            if (!sPackageName.equals(pkg)) {
                log("startActivity: cross-package component not supported: "
                        + pkg + "/" + cls);
                return null;
            }
            Object info = findParsedActivity(sParsedPackage, cls);
            if (info == null) {
                log("startActivity: activity not declared in manifest: " + cls);
            }
            return info;
        }

        String action = (String) intentClass.getMethod("getAction").invoke(intent);
        String type = (String) intentClass.getMethod("getType").invoke(intent);
        String scheme = (String) intentClass.getMethod("getScheme").invoke(intent);
        Object data = intentClass.getMethod("getData").invoke(intent);
        Set<?> categories = (Set<?>) intentClass.getMethod("getCategories").invoke(intent);
        Class<?> uriClass = Class.forName("android.net.Uri");
        List<?> activities = (List<?>) getFieldValue(sParsedPackage, "activities");
        for (Object activity : activities) {
            List<?> filters = (List<?>) getFieldValue(activity, "intents");
            for (Object filter : filters) {
                int match = (Integer) filter.getClass()
                        .getMethod("match", String.class, String.class, String.class,
                                uriClass, Set.class, String.class)
                        .invoke(filter, action, type, scheme, data, categories, "droidlet");
                if (match >= 0) {
                    return getFieldValue(activity, "info");
                }
            }
        }
        log("startActivity: no activity matches " + intent);
        return null;
    }

    static void handleStartActivity(Object intent, Object resultTo,
            String resultWho, int requestCode) {
        try {
            Object info = resolveActivityInfo(intent);
            if (info == null) {
                return;
            }
            final String className = (String) getFieldValue(info, "name");
            final int flags = (Integer) intent.getClass().getMethod("getFlags").invoke(intent);
            final int launchMode = (Integer) getFieldValue(info, "launchMode");
            final int singleTopFlag = staticInt(
                    Class.forName("android.content.Intent"),
                    "FLAG_ACTIVITY_SINGLE_TOP", 0x20000000);
            final int clearTopFlag = staticInt(
                    Class.forName("android.content.Intent"),
                    "FLAG_ACTIVITY_CLEAR_TOP", 0x04000000);
            ActivityRecord top = topRecord();

            if ((launchMode == 1 || (flags & singleTopFlag) != 0)
                    && top != null && top.className.equals(className)) {
                log("startActivity: singleTop reuse " + className);
                activateInputChannel(top);
                scheduleNewIntent(top, intent, !top.resumed);
                top.resumed = true;
                sResumedRecord = top;
                return;
            }

            if ((flags & clearTopFlag) != 0) {
                ActivityRecord existing = findRecordByClassName(className);
                if (existing != null) {
                    log("startActivity: CLEAR_TOP reuse " + className);
                    finishRecordsAbove(existing);
                    activateInputChannel(existing);
                    scheduleNewIntent(existing, intent, !existing.resumed);
                    existing.resumed = true;
                    sResumedRecord = existing;
                    return;
                }
            }

            ActivityRecord record = new ActivityRecord();
            record.token = newBinder();
            record.intent = intent;
            record.info = info;
            record.className = className;
            record.resultTo = resultTo;
            record.resultWho = resultWho;
            record.requestCode = requestCode;
            record.inputChannelId = -1;

            ActivityRecord previous = top;
            if (sResumedRecord != null) {
                schedulePause(sResumedRecord, false);
                sResumedRecord.resumed = false;
                sResumedRecord = null;
            }
            sTaskStack.add(record);
            log("startActivity: launching " + className);
            scheduleLaunch(record);
            scheduleResume(record);
            record.resumed = true;
            sResumedRecord = record;
            if (previous != null && previous != record && !previous.finished) {
                scheduleStop(previous);
            }
        } catch (Throwable t) {
            log("startActivity failed: " + t);
            t.printStackTrace(System.err);
        }
    }

    static void finishRecordsAbove(ActivityRecord target) throws Exception {
        int index = sTaskStack.indexOf(target);
        if (index < 0) {
            return;
        }
        for (int i = sTaskStack.size() - 1; i > index; i--) {
            ActivityRecord record = sTaskStack.remove(i);
            record.finished = true;
            schedulePause(record, true);
            scheduleDestroy(record, true);
            if (sResumedRecord == record) {
                sResumedRecord = null;
            }
        }
    }

    static void handleFinishActivity(Object token, int resultCode, Object resultData,
            boolean finishTask) throws Exception {
        ActivityRecord record = findRecordByToken(token);
        if (record == null || record.finished) {
            return;
        }
        log("finishActivity: " + record.className);

        if (finishTask) {
            finishAllActivities(resultCode, resultData);
            return;
        }

        record.finished = true;
        sTaskStack.remove(record);
        schedulePause(record, true);

        ActivityRecord resultCaller = null;
        if (record.requestCode >= 0 && record.resultTo != null) {
            resultCaller = findRecordByToken(record.resultTo);
            if (resultCaller != null && !resultCaller.finished) {
                scheduleResult(resultCaller, record.resultWho, record.requestCode,
                        resultCode, resultData, record.token);
            } else {
                resultCaller = null;
            }
        }
        scheduleDestroy(record, true);
        if (sResumedRecord == record) {
            sResumedRecord = null;
        }

        ActivityRecord next = topRecord();
        if (resultCaller != null && resultCaller == next) {
            // ActivityResultItem's post-execution state is ON_RESUME, so the
            // transaction executor brings the caller through onStart/onResume.
            // Do not schedule a second resume here.
            activateInputChannel(resultCaller);
            resultCaller.resumed = true;
            sResumedRecord = resultCaller;
        } else if (next != null) {
            activateInputChannel(next);
            scheduleStart(next);
            scheduleResume(next);
            next.resumed = true;
            sResumedRecord = next;
        } else {
            scheduleExitIfEmpty();
        }
    }

    static void finishAllActivities(int resultCode, Object resultData) throws Exception {
        for (int i = sTaskStack.size() - 1; i >= 0; i--) {
            ActivityRecord record = sTaskStack.get(i);
            record.finished = true;
            if (record.resumed) {
                schedulePause(record, true);
            }
            if (record.requestCode >= 0 && record.resultTo != null) {
                ActivityRecord caller = findRecordByToken(record.resultTo);
                if (caller != null && !caller.finished) {
                    scheduleResult(caller, record.resultWho, record.requestCode,
                            resultCode, resultData, record.token);
                }
            }
            scheduleDestroy(record, true);
            record.resumed = false;
        }
        sTaskStack.clear();
        sResumedRecord = null;
        scheduleExitIfEmpty();
    }

    static void activateInputChannel(ActivityRecord record) {
        if (record == null || record.inputChannelId <= 0) {
            return;
        }
        activateTopInputChannel();
        log("input: activity resumed " + record.className
                + " channel=" + record.inputChannelId);
    }

    static void scheduleTransactionItem(Object item) throws Exception {
        Object transaction = sClientTransactionClass
                .getConstructor(sIApplicationThreadClass)
                .newInstance(sAppThread);
        Method add = sClientTransactionClass.getMethod(
                "addTransactionItem", sClientTransactionItemClass);
        add.invoke(transaction, item);
        Method schedule = sIApplicationThreadClass.getMethod(
                "scheduleTransaction", sClientTransactionClass);
        schedule.invoke(sAppThread, transaction);
    }

    static void schedulePause(ActivityRecord record, boolean finished) throws Exception {
        if (record == null || !record.launched) {
            return;
        }
        Class<?> itemClass =
                Class.forName("android.app.servertransaction.PauseActivityItem");
        Object item = itemClass.getConstructor(
                Class.forName("android.os.IBinder"), boolean.class, boolean.class,
                boolean.class, boolean.class)
                .newInstance(record.token, finished, true /* userLeaving */,
                        false /* dontReport */, false /* autoEnteringPip */);
        scheduleTransactionItem(item);
    }

    static void scheduleStop(ActivityRecord record) throws Exception {
        if (record == null || !record.launched || record.finished || record.stopped) {
            return;
        }
        Class<?> itemClass =
                Class.forName("android.app.servertransaction.StopActivityItem");
        Object item = itemClass.getConstructor(Class.forName("android.os.IBinder"))
                .newInstance(record.token);
        scheduleTransactionItem(item);
        record.stopped = true;
    }

    static void scheduleStart(ActivityRecord record) throws Exception {
        if (record == null || !record.launched || record.finished || !record.stopped) {
            return;
        }
        Class<?> itemClass =
                Class.forName("android.app.servertransaction.StartActivityItem");
        Object item = itemClass.getConstructor(
                Class.forName("android.os.IBinder"),
                Class.forName("android.app.ActivityOptions$SceneTransitionInfo"))
                .newInstance(record.token, null);
        scheduleTransactionItem(item);
        record.stopped = false;
    }

    static void scheduleDestroy(ActivityRecord record, boolean finished) throws Exception {
        if (record == null || !record.launched) {
            return;
        }
        Class<?> itemClass =
                Class.forName("android.app.servertransaction.DestroyActivityItem");
        Object item = itemClass.getConstructor(
                Class.forName("android.os.IBinder"), boolean.class)
                .newInstance(record.token, finished);
        scheduleTransactionItem(item);
        record.launched = false;
    }

    static void scheduleNewIntent(ActivityRecord record, Object intent, boolean resume)
            throws Exception {
        Class<?> referrerClass =
                Class.forName("com.android.internal.content.ReferrerIntent");
        Object referrer = referrerClass.getConstructor(
                Class.forName("android.content.Intent"), String.class)
                .newInstance(intent, null);
        ArrayList<Object> intents = new ArrayList<>();
        intents.add(referrer);
        Class<?> itemClass =
                Class.forName("android.app.servertransaction.NewIntentItem");
        Object item = itemClass.getConstructor(
                Class.forName("android.os.IBinder"), List.class, boolean.class)
                .newInstance(record.token, intents, resume);
        scheduleTransactionItem(item);
        record.intent = intent;
    }

    static void scheduleResult(ActivityRecord caller, String resultWho,
            int requestCode, int resultCode, Object data, Object callerToken)
            throws Exception {
        Class<?> resultClass = Class.forName("android.app.ResultInfo");
        Object result = resultClass.getConstructor(
                String.class, int.class, int.class, Class.forName("android.content.Intent"),
                Class.forName("android.os.IBinder"))
                .newInstance(resultWho, requestCode, resultCode, data, callerToken);
        ArrayList<Object> results = new ArrayList<>();
        results.add(result);
        Class<?> itemClass =
                Class.forName("android.app.servertransaction.ActivityResultItem");
        Object item = itemClass.getConstructor(
                Class.forName("android.os.IBinder"), List.class)
                .newInstance(caller.token, results);
        scheduleTransactionItem(item);
    }

    static void scheduleLaunch(ActivityRecord record) throws Exception {
        record.launched = true;
        Object transaction = sClientTransactionClass
                .getConstructor(sIApplicationThreadClass)
                .newInstance(sAppThread);

        Object item = makeLaunchActivityItem(record);
        Method add = sClientTransactionClass.getMethod(
                "addTransactionItem", sClientTransactionItemClass);
        add.invoke(transaction, item);

        Method schedule = sIApplicationThreadClass.getMethod(
                "scheduleTransaction", sClientTransactionClass);
        schedule.invoke(sAppThread, transaction);
    }

    static void scheduleResume(ActivityRecord record) throws Exception {
        Object transaction = sClientTransactionClass
                .getConstructor(sIApplicationThreadClass)
                .newInstance(sAppThread);

        Object item = makeResumeActivityItem(record);
        Method add = sClientTransactionClass.getMethod(
                "addTransactionItem", sClientTransactionItemClass);
        add.invoke(transaction, item);

        Method schedule = sIApplicationThreadClass.getMethod(
                "scheduleTransaction", sClientTransactionClass);
        schedule.invoke(sAppThread, transaction);
    }

    static Object makeResumeActivityItem(ActivityRecord record) throws Exception {
        Class<?> itemClass =
                Class.forName("android.app.servertransaction.ResumeActivityItem");
        Constructor<?> ctor = itemClass.getConstructor(
                Class.forName("android.os.IBinder"), boolean.class, boolean.class);
        // isForward=true, shouldSendCompatFakeFocus=false.  The compat focus
        // path calls into ViewRootImpl.dispatchCompatFakeFocus(), which is not
        // needed for the lifecycle milestone and would queue extra input work.
        return ctor.newInstance(record.token, true, false);
    }

    static Object makeLaunchActivityItem(ActivityRecord record) throws Exception {
        Class<?> itemClass =
                Class.forName("android.app.servertransaction.LaunchActivityItem");
        Constructor<?> ctor = null;
        for (Constructor<?> candidate : itemClass.getConstructors()) {
            if (candidate.getParameterCount() == 25) {
                ctor = candidate;
                break;
            }
        }
        if (ctor == null) {
            throw new IllegalStateException("LaunchActivityItem constructor not found");
        }

        Class<?>[] types = ctor.getParameterTypes();
        Object[] args = new Object[types.length];

        sActivityToken = record.token;
        Object assistToken = newBinder();
        Object shareableToken = newBinder();
        Object initialCallerToken = newBinder();
        Object intent = record.intent;
        Object config = makeConfiguration();
        Object windowInfo = Class.forName("android.window.ActivityWindowInfo")
                .getConstructor().newInstance();
        int procState = staticInt(
                Class.forName("android.app.ActivityManager"), "PROCESS_STATE_TOP", 2);

        for (int i = 0; i < types.length; i++) {
            String name = types[i].getName();
            if ("android.os.IBinder".equals(name)) {
                if (i == 0) {
                    args[i] = sActivityToken;
                } else if (i == 17) {
                    args[i] = assistToken;
                } else if (i == 19) {
                    args[i] = shareableToken;
                } else if (i == 22) {
                    args[i] = initialCallerToken;
                } else {
                    args[i] = null;
                }
            } else if ("android.content.Intent".equals(name)) {
                args[i] = intent;
            } else if ("int".equals(name)) {
                if (i == 9) {
                    args[i] = procState;
                } else if (i == 24) {
                    args[i] = sDisplayId;
                } else {
                    args[i] = 0;
                }
            } else if ("android.content.pm.ActivityInfo".equals(name)) {
                args[i] = record.info;
            } else if ("android.content.res.Configuration".equals(name)) {
                args[i] = config;
            } else if ("boolean".equals(name)) {
                args[i] = false;
            } else if ("android.app.IActivityClientController".equals(name)) {
                args[i] = sActivityClientController;
            } else if ("android.window.ActivityWindowInfo".equals(name)) {
                args[i] = windowInfo;
            } else {
                args[i] = null;
            }
        }
        return ctor.newInstance(args);
    }

    static Object makeIntent() throws Exception {
        Class<?> intentClass = Class.forName("android.content.Intent");
        String startUri = System.getenv("DROIDLET_START_URI");
        if (startUri != null && !startUri.isEmpty()) {
            Class<?> uriClass = Class.forName("android.net.Uri");
            Object uri = uriClass.getMethod("parse", String.class).invoke(null, startUri);
            String action = (String) intentClass.getField("ACTION_VIEW").get(null);
            Object intent = intentClass.getConstructor(String.class, uriClass)
                    .newInstance(action, uri);
            String type = System.getenv("DROIDLET_START_TYPE");
            if (type != null && !type.isEmpty()) {
                // Intent.setType() clears the data URI; set both together.
                intentClass.getMethod("setDataAndType", uriClass, String.class)
                        .invoke(intent, uri, type);
            }
            String defaultCategory = (String) intentClass.getField("CATEGORY_DEFAULT").get(null);
            intentClass.getMethod("addCategory", String.class)
                    .invoke(intent, defaultCategory);
            return intent;
        }
        Object intent = intentClass.getDeclaredConstructor().newInstance();
        intentClass.getMethod("setClassName", String.class, String.class)
                .invoke(intent, sPackageName, sClassName);
        return intent;
    }

    static Object makeConfiguration() throws Exception {
        final int width = displayWidth();
        final int height = displayHeight();
        final int densityDpi = displayDensityDpi();
        final int widthDp = pxToDp(width);
        final int heightDp = pxToDp(height);
        Class<?> configClass = Class.forName("android.content.res.Configuration");
        Object config = configClass.getDeclaredConstructor().newInstance();
        setField(config, "densityDpi", densityDpi);
        setField(config, "fontScale", 1.0f);
        setField(config, "orientation", width > height ? 2 : 1);
        setField(config, "screenWidthDp", widthDp);
        setField(config, "screenHeightDp", heightDp);
        setField(config, "smallestScreenWidthDp", Math.min(widthDp, heightDp));
        setField(config, "locale", java.util.Locale.getDefault());
        // ActivityTaskManager normally fills the window configuration bounds from the
        // display.  ViewRootImpl uses winConfig.getBounds() as the display frame when it
        // computes window frames locally (relayoutAsync), so leaving these at zero makes
        // every window 1x1 and skips drawing entirely.
        Object windowConfig = configClass.getField("windowConfiguration").get(config);
        Class<?> windowConfigClass = Class.forName("android.app.WindowConfiguration");
        Class<?> rectClass = Class.forName("android.graphics.Rect");
        Object bounds = rectClass.getConstructor(int.class, int.class, int.class, int.class)
                .newInstance(0, 0, width, height);
        windowConfigClass.getMethod("setBounds", rectClass).invoke(windowConfig, bounds);
        windowConfigClass.getMethod("setMaxBounds", rectClass).invoke(windowConfig, bounds);
        windowConfigClass.getMethod("setAppBounds", rectClass).invoke(windowConfig, bounds);
        // WindowConfiguration.WINDOWING_MODE_FULLSCREEN.
        windowConfigClass.getMethod("setWindowingMode", int.class).invoke(windowConfig, 1);
        return config;
    }

    static Object compatibilityInfo() throws Exception {
        Class<?> compatClass = Class.forName("android.content.res.CompatibilityInfo");
        Field field = compatClass.getField("DEFAULT_COMPATIBILITY_INFO");
        return field.get(null);
    }

}
