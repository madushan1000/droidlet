/*
 * Broadcast, sticky-broadcast and PendingIntent delivery for DroidletLauncher.
 *
 * One package runs per process, so the launcher is its own ActivityManager:
 * receivers registered by the app and the manifest receivers of the launched
 * package are resolved locally, ordered broadcasts are delivered one at a
 * time from a queue, and PendingIntent records dispatch through the same
 * activity/service/broadcast entry points.
 */

package android.app;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.ArrayList;
import java.util.List;

import static android.app.DroidletLauncher.*;
import static android.app.DroidletLauncherServices.*;
import static android.app.DroidletLauncherStartup.*;
import static android.app.DroidletLauncherActivities.*;

final class DroidletLauncherBroadcasts {
    private DroidletLauncherBroadcasts() {}

    private static final ArrayList<OrderedBroadcast> sOrderedQueue = new ArrayList<>();
    private static OrderedBroadcast sActiveOrderedBroadcast;
    private static final ArrayList<DeliveryToken> sOutstandingFinishes = new ArrayList<>();

    static boolean hasPendingBroadcastWork() {
        synchronized (sBroadcastLock) {
            return sActiveOrderedBroadcast != null || !sOrderedQueue.isEmpty()
                    || !sOutstandingFinishes.isEmpty();
        }
    }

    static final class RegisteredReceiver {
        Object receiver;
        Object filter;
        String packageName;
        String permission;
        int userId;
        int flags;
    }

    static final class BroadcastTarget {
        boolean registered;
        Object receiver;
        Object activityInfo;
        String className;
    }

    static final class DeliveryToken {
        boolean ordered;
        OrderedBroadcast record;
    }

    static final class OrderedBroadcast {
        Object intent;
        Object resultTo;
        int resultCode;
        String resultData;
        Object extras;
        int sendingUser;
        final ArrayList<BroadcastTarget> targets = new ArrayList<>();
        int next;
        boolean aborted;
    }

    static final class PendingIntentRecord {
        int type;
        String packageName;
        Object intent;
        int requestCode;
        Object sender;
        int flags;
        boolean canceled;

        void send(int code, Object fillIn, Object finishedReceiver) {
            log("pending intent: send type=" + type + " canceled=" + canceled);
            if (canceled) {
                return;
            }
            final Object target = fillIn != null ? fillIn : intent;
            if (target == null) {
                return;
            }
            try {
                if (type == 1) { // INTENT_SENDER_BROADCAST
                    handleBroadcastIntent(target, finishedReceiver, code, null, null,
                            false, false, null, 0);
                } else if (type == 2) { // INTENT_SENDER_ACTIVITY
                    postToActivityThread(new Runnable() {
                        @Override
                        public void run() {
                            handleStartActivity(target, null, null, 0);
                        }
                    });
                } else if (type == 4 || type == 5) { // SERVICE / FOREGROUND_SERVICE
                    handleStartService(target, type == 5);
                }
            } catch (Throwable t) {
                log("pending intent send failed: " + t);
            }
        }
    }

    static Object copyIntent(Object intent) throws Exception {
        if (intent == null) {
            return null;
        }
        Class<?> intentClass = Class.forName("android.content.Intent");
        return intentClass.getConstructor(intentClass).newInstance(intent);
    }

    static String intentString(Object intent, String method) {
        try {
            return (String) intent.getClass().getMethod(method).invoke(intent);
        } catch (Throwable t) {
            return null;
        }
    }

    static void registerReceiverRecord(Object receiver, Object filter, String packageName,
            String permission, int userId, int flags) {
        if (receiver == null) {
            return;
        }
        synchronized (sBroadcastLock) {
            for (RegisteredReceiver record : sRegisteredReceivers) {
                if (record.receiver == receiver) {
                    record.filter = filter;
                    record.packageName = packageName;
                    record.permission = permission;
                    record.userId = userId;
                    record.flags = flags;
                    return;
                }
            }
            RegisteredReceiver record = new RegisteredReceiver();
            record.receiver = receiver;
            record.filter = filter;
            record.packageName = packageName;
            record.permission = permission;
            record.userId = userId;
            record.flags = flags;
            sRegisteredReceivers.add(record);
        }
        log("broadcast: registered receiver in " + packageName);
    }

    static void unregisterReceiverRecord(Object receiver) {
        synchronized (sBroadcastLock) {
            for (int i = sRegisteredReceivers.size() - 1; i >= 0; i--) {
                if (sRegisteredReceivers.get(i).receiver == receiver) {
                    sRegisteredReceivers.remove(i);
                }
            }
        }
    }

    static Object findStickyBroadcast(Object filter) {
        synchronized (sBroadcastLock) {
            for (Object sticky : sStickyBroadcasts) {
                try {
                    if (intentFilterMatches(filter, sticky)) {
                        return copyIntent(sticky);
                    }
                } catch (Throwable ignored) {
                }
            }
        }
        return null;
    }

    static void addStickyBroadcast(Object intent) throws Exception {
        synchronized (sBroadcastLock) {
            for (int i = sStickyBroadcasts.size() - 1; i >= 0; i--) {
                if (intentEquals(sStickyBroadcasts.get(i), intent)) {
                    sStickyBroadcasts.remove(i);
                }
            }
            sStickyBroadcasts.add(copyIntent(intent));
        }
    }

    static ArrayList<BroadcastTarget> resolveBroadcastTargets(Object intent)
            throws Exception {
        ArrayList<BroadcastTarget> targets = new ArrayList<>();
        if (intent == null) {
            return targets;
        }
        Object component = intent.getClass().getMethod("getComponent").invoke(intent);
        String explicitClass = null;
        if (component != null) {
            String pkg = componentField(component, "getPackageName");
            explicitClass = componentField(component, "getClassName");
            if (pkg != null && !pkg.equals(sPackageName)) {
                log("broadcast: refusing cross-package component " + pkg + "/" + explicitClass);
                return targets;
            }
        }

        synchronized (sBroadcastLock) {
            for (RegisteredReceiver record : sRegisteredReceivers) {
                try {
                    Object filter = record.filter;
                    if (explicitClass != null) {
                        // Registered receivers never match an explicit component
                        // unless the caller supplied their filter action too.
                        if (intentString(intent, "getAction") != null &&
                                filter != null && intentFilterMatches(filter, intent)) {
                            BroadcastTarget target = new BroadcastTarget();
                            target.registered = true;
                            target.receiver = record.receiver;
                            targets.add(target);
                        }
                    } else if (filter == null || intentFilterMatches(filter, intent)) {
                        BroadcastTarget target = new BroadcastTarget();
                        target.registered = true;
                        target.receiver = record.receiver;
                        targets.add(target);
                    }
                } catch (Throwable ignored) {
                }
            }
        }

        if (sParsedPackage == null) {
            return targets;
        }
        List<?> receivers = (List<?>) getFieldValue(sParsedPackage, "receivers");
        for (Object parsed : receivers) {
            Object info = getFieldValue(parsed, "info");
            String className = (String) getFieldValue(info, "name");
            if (explicitClass != null && !explicitClass.equals(className)) {
                continue;
            }
            boolean matches = explicitClass != null;
            if (!matches) {
                List<?> filters = (List<?>) getFieldValue(parsed, "intents");
                for (Object filter : filters) {
                    try {
                        if (intentFilterMatches(filter, intent)) {
                            matches = true;
                            break;
                        }
                    } catch (Throwable ignored) {
                    }
                }
            }
            if (!matches) {
                continue;
            }
            Object generated = generateComponentInfo("Activity", parsed, 0);
            if (generated == null) {
                continue;
            }
            setField(generated, "applicationInfo", sAppInfo);
            BroadcastTarget target = new BroadcastTarget();
            target.activityInfo = generated;
            target.className = className;
            targets.add(target);
        }
        return targets;
    }

    static void deliverRegisteredReceiver(Object receiver, Object intent, int resultCode,
            String resultData, Object extras, boolean ordered, boolean sticky, int sendingUser) {
        try {
            // LoadedApk.InnerReceiver has an extended overload that reports
            // whether delivery needs to be acknowledged.  Use it when present.
            try {
                receiver.getClass().getMethod("performReceive",
                                Class.forName("android.content.Intent"), int.class,
                                String.class, Class.forName("android.os.Bundle"),
                                boolean.class, boolean.class, boolean.class, int.class,
                                int.class, String.class)
                        .invoke(receiver, intent, resultCode, resultData, extras, ordered,
                                sticky, false, sendingUser, sUid, sPackageName);
            } catch (NoSuchMethodException e) {
                receiver.getClass().getMethod("performReceive",
                                Class.forName("android.content.Intent"), int.class,
                                String.class, Class.forName("android.os.Bundle"),
                                boolean.class, boolean.class, int.class)
                        .invoke(receiver, intent, resultCode, resultData, extras, ordered, sticky,
                                sendingUser);
            }
        } catch (Throwable t) {
            log("broadcast: registered delivery failed: " + t);
        }
    }

    static void deliverManifestReceiver(BroadcastTarget target, Object intent,
            int resultCode, String resultData, Object extras, boolean ordered, int sendingUser) {
        try {
            Object copy = copyIntent(intent);
            copy.getClass().getMethod("setClassName", String.class, String.class)
                    .invoke(copy, sPackageName, target.className);
            appThreadInvoke("scheduleReceiver",
                    new Class<?>[] {
                            Class.forName("android.content.Intent"),
                            Class.forName("android.content.pm.ActivityInfo"),
                            Class.forName("android.content.res.CompatibilityInfo"),
                            int.class, String.class, Class.forName("android.os.Bundle"),
                            boolean.class, boolean.class, int.class, int.class, int.class,
                            String.class},
                    copy, target.activityInfo, defaultCompatibilityInfo(), resultCode, resultData,
                    extras, ordered, false, sendingUser, serviceProcessState(), sUid,
                    sPackageName);
        } catch (Throwable t) {
            log("broadcast: manifest delivery failed: " + t);
        }
    }

    static void deliverBroadcastTarget(BroadcastTarget target, Object intent,
            int resultCode, String resultData, Object extras, boolean ordered, int sendingUser) {
        DeliveryToken token = new DeliveryToken();
        token.ordered = ordered;
        token.record = ordered ? sActiveOrderedBroadcast : null;
        synchronized (sBroadcastLock) {
            sOutstandingFinishes.add(token);
        }
        if (target.registered) {
            deliverRegisteredReceiver(target.receiver, intent, resultCode, resultData, extras,
                    ordered, false, sendingUser);
        } else if (target.activityInfo != null) {
            deliverManifestReceiver(target, intent, resultCode, resultData, extras, ordered,
                    sendingUser);
        }
    }

    static int handleBroadcastIntent(Object intent, Object resultTo, int resultCode,
            String resultData, Object extras, boolean serialized, boolean sticky,
            String[] excludePackages, int userId) throws Exception {
        if (intent == null) {
            return 0;
        }
        if (excludePackages != null) {
            for (String excluded : excludePackages) {
                if (sPackageName.equals(excluded)) {
                    return resultCode;
                }
            }
        }
        if (sticky) {
            addStickyBroadcast(intent);
        }
        ArrayList<BroadcastTarget> targets = resolveBroadcastTargets(intent);
        log("broadcast: " + intentString(intent, "getAction") + " targets=" + targets.size()
                + (serialized ? " ordered" : ""));
        if (!serialized) {
            for (BroadcastTarget target : targets) {
                deliverBroadcastTarget(target, intent, resultCode, resultData, extras, false, 0);
            }
            return resultCode;
        }
        OrderedBroadcast record = new OrderedBroadcast();
        record.intent = intent;
        record.resultTo = resultTo;
        record.resultCode = resultCode;
        record.resultData = resultData;
        record.extras = extras;
        record.sendingUser = 0;
        record.targets.addAll(targets);
        synchronized (sBroadcastLock) {
            sOrderedQueue.add(record);
        }
        startNextOrderedBroadcast();
        return resultCode;
    }

    static void startNextOrderedBroadcast() {
        synchronized (sBroadcastLock) {
            if (sActiveOrderedBroadcast != null || sOrderedQueue.isEmpty()) {
                return;
            }
            sActiveOrderedBroadcast = sOrderedQueue.remove(0);
        }
        postToActivityThread(new Runnable() {
            @Override
            public void run() {
                deliverNextOrderedReceiver();
            }
        });
    }

    static void deliverNextOrderedReceiver() {
        OrderedBroadcast record;
        BroadcastTarget target = null;
        synchronized (sBroadcastLock) {
            record = sActiveOrderedBroadcast;
            if (record == null) {
                return;
            }
            if (record.next >= record.targets.size() || record.aborted) {
                finishOrderedBroadcast(record);
                return;
            }
            target = record.targets.get(record.next++);
        }
        deliverBroadcastTarget(target, record.intent, record.resultCode, record.resultData,
                record.extras, true, record.sendingUser);
    }

    static void finishOrderedBroadcast(OrderedBroadcast record) {
        synchronized (sBroadcastLock) {
            if (sActiveOrderedBroadcast == record) {
                sActiveOrderedBroadcast = null;
            }
        }
        if (record.resultTo != null) {
            try {
                DeliveryToken token = new DeliveryToken();
                token.ordered = false;
                synchronized (sBroadcastLock) {
                    sOutstandingFinishes.add(token);
                }
                deliverRegisteredReceiver(record.resultTo, record.intent, record.resultCode,
                        record.resultData, record.extras, false, false, record.sendingUser);
            } catch (Throwable t) {
                log("broadcast: result delivery failed: " + t);
            }
        }
        log("broadcast: ordered complete code=" + record.resultCode
                + (record.aborted ? " aborted" : ""));
        startNextOrderedBroadcast();
        scheduleExitIfEmpty();
    }

    static void handleFinishReceiver(Object who, int resultCode, String resultData,
            Object extras, boolean abortBroadcast) {
        DeliveryToken token;
        synchronized (sBroadcastLock) {
            if (sOutstandingFinishes.isEmpty()) {
                return;
            }
            token = sOutstandingFinishes.remove(0);
        }
        if (!token.ordered || token.record == null) {
            postToActivityThread(new Runnable() {
                @Override
                public void run() {
                    scheduleExitIfEmpty();
                }
            });
            return;
        }
        synchronized (sBroadcastLock) {
            if (sActiveOrderedBroadcast != token.record) {
                return;
            }
            token.record.resultCode = resultCode;
            token.record.resultData = resultData;
            token.record.extras = extras;
            if (abortBroadcast) {
                token.record.aborted = true;
            }
        }
        postToActivityThread(new Runnable() {
            @Override
            public void run() {
                deliverNextOrderedReceiver();
            }
        });
    }

    static Object newIntentSender(final PendingIntentRecord record) throws Exception {
        final Object binder = newBinder();
        Class<?> iface = Class.forName("android.content.IIntentSender");
        Object proxy = Proxy.newProxyInstance(iface.getClassLoader(), new Class<?>[] {iface},
                new InvocationHandler() {
                    @Override
                    public Object invoke(Object p, Method method, Object[] args) {
                        String name = method.getName();
                        if ("asBinder".equals(name)) {
                            return binder;
                        }
                        if ("send".equals(name)) {
                            record.send((Integer) args[0], args[1], args[4]);
                            return null;
                        }
                        return defaultValue(method.getReturnType());
                    }
                });
        Class.forName("android.os.Binder")
                .getMethod("attachInterface", Class.forName("android.os.IInterface"),
                        String.class)
                .invoke(binder, proxy, "android.content.IIntentSender");
        record.sender = proxy;
        synchronized (sPendingIntentLock) {
            sPendingIntents.put(proxy, record);
        }
        return proxy;
    }

    static Object handleGetIntentSender(int type, String packageName, Object token,
            String resultWho, int requestCode, Object[] intents, String[] resolvedTypes,
            int flags, Object options, int userId) throws Exception {
        Object chosen = null;
        if (intents != null) {
            for (Object intent : intents) {
                if (intent != null) {
                    chosen = intent;
                }
            }
        }
        String pkg = packageName != null ? packageName : sPackageName;
        synchronized (sPendingIntentLock) {
            for (PendingIntentRecord existing : sPendingIntents.values()) {
                if (!existing.canceled && existing.type == type
                        && pkg.equals(existing.packageName)
                        && existing.requestCode == requestCode
                        && intentEquals(existing.intent, chosen)) {
                    existing.intent = chosen;
                    existing.flags = flags;
                    return existing.sender;
                }
            }
        }
        PendingIntentRecord record = new PendingIntentRecord();
        record.type = type;
        record.packageName = pkg;
        record.intent = chosen;
        record.requestCode = requestCode;
        record.flags = flags;
        return newIntentSender(record);
    }

    static void cancelIntentSender(Object sender) {
        synchronized (sPendingIntentLock) {
            PendingIntentRecord record = sPendingIntents.remove(sender);
            if (record != null) {
                record.canceled = true;
            }
        }
    }

}
