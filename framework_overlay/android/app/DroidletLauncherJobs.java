/*
 * Alarms and jobs for DroidletLauncher.
 *
 * There is no system server: the AlarmManager schedule and the JobScheduler
 * queue are kept in-process and dispatched to components of the launched
 * package.  Periodic alarms and jobs reuse the same single-threaded executor;
 * a job runs its manifest service and reports completion through a local
 * IJobCallback binder.
 */

package android.app;

import android.os.Binder;
import android.os.IInterface;
import android.os.Parcel;
import android.os.RemoteException;

import java.lang.reflect.Constructor;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;

import static android.app.DroidletLauncher.*;
import static android.app.DroidletLauncherBroadcasts.*;
import static android.app.DroidletLauncherServices.*;
import static android.app.DroidletLauncherStartup.*;

final class DroidletLauncherJobs {
    private DroidletLauncherJobs() {}

    static final Object sJobLock = new Object();
    static final Map<Integer, JobRecord> sJobs = new HashMap<>();
    static final Map<Integer, JobRecord> sRunningJobs = new HashMap<>();

    static final Object sAlarmLock = new Object();
    static final ArrayList<AlarmRecord> sAlarms = new ArrayList<>();
    static final java.util.concurrent.ScheduledThreadPoolExecutor sAlarmExecutor =
            new java.util.concurrent.ScheduledThreadPoolExecutor(1, r -> {
                Thread thread = new Thread(r, "droidlet-alarms");
                thread.setDaemon(true);
                return thread;
            });

    static final class AlarmRecord {
        String packageName;
        int type;
        long triggerAt;
        long interval;
        Object operation;
        Object listener;
        String listenerTag;
        java.util.concurrent.ScheduledFuture<?> future;
    }

    static final class JobRecord {
        int jobId;
        String namespace;
        Object info;
        Object serviceInfo;
        Object connection;
        Object binder;
        Object params;
        java.util.concurrent.ScheduledFuture<?> future;
        boolean running;
        boolean finished;
        boolean periodic;
        long interval;
    }

    static final class JobCallbackBinder extends Binder {
        private static final String DESCRIPTOR = "android.app.job.IJobCallback";
        private final JobRecord mRecord;

        JobCallbackBinder(JobRecord record) {
            mRecord = record;
        }

        @Override
        protected boolean onTransact(int code, Parcel data, Parcel reply, int flags)
                throws RemoteException {
            switch (code) {
                case 3: { // acknowledgeStartMessage
                    data.enforceInterface(DESCRIPTOR);
                    data.readInt();
                    if (!data.readBoolean()) {
                        finishJob(mRecord, false);
                    }
                    return true;
                }
                case 4: { // acknowledgeStopMessage
                    data.enforceInterface(DESCRIPTOR);
                    data.readInt();
                    finishJob(mRecord, data.readBoolean());
                    return true;
                }
                case 5: // dequeueWork
                    reply.writeNoException();
                    reply.writeInt(0);
                    return true;
                case 6: { // completeWork
                    data.enforceInterface(DESCRIPTOR);
                    data.readInt();
                    data.readInt();
                    reply.writeNoException();
                    reply.writeInt(1);
                    return true;
                }
                case 7: { // jobFinished
                    data.enforceInterface(DESCRIPTOR);
                    data.readInt();
                    finishJob(mRecord, data.readBoolean());
                    return true;
                }
                default:
                    return super.onTransact(code, data, reply, flags);
            }
        }
    }

    static long elapsedRealtimeMillis() {
        try {
            return (Long) Class.forName("android.os.SystemClock")
                    .getMethod("elapsedRealtime").invoke(null);
        } catch (Throwable t) {
            return System.nanoTime() / 1_000_000L;
        }
    }

    static void scheduleAlarm(AlarmRecord record) {
        long now = (record.type == 2 || record.type == 3)
                ? elapsedRealtimeMillis() : System.currentTimeMillis();
        long delay = record.triggerAt - now;
        if (delay < 0) {
            delay = 0;
        }
        record.future = sAlarmExecutor.schedule(new Runnable() {
            @Override
            public void run() {
                fireAlarm(record);
            }
        }, delay, java.util.concurrent.TimeUnit.MILLISECONDS);
    }

    static void fireAlarm(AlarmRecord record) {
        synchronized (sAlarmLock) {
            if (!sAlarms.contains(record)) {
                return;
            }
            if (record.interval <= 0) {
                sAlarms.remove(record);
            } else {
                record.triggerAt = ((record.type == 2 || record.type == 3)
                        ? elapsedRealtimeMillis() : System.currentTimeMillis()) + record.interval;
                scheduleAlarm(record);
            }
        }
        log("alarm: fired " + record.packageName + " type=" + record.type);
        try {
            if (record.operation != null) {
                record.operation.getClass().getMethod("send").invoke(record.operation);
            } else if (record.listener != null) {
                Object binder = record.listener.getClass().getMethod("asBinder")
                        .invoke(record.listener);
                Object iface = Class.forName("android.app.IAlarmListener$Stub")
                        .getMethod("asInterface", Class.forName("android.os.IBinder"))
                        .invoke(null, binder);
                iface.getClass().getMethod("onAlarm").invoke(iface);
            }
        } catch (Throwable t) {
            log("alarm: dispatch failed: " + t);
        }
        scheduleExitIfEmpty();
    }

    static void handleSetAlarm(String packageName, int type, long triggerAt,
            long interval, Object operation, Object listener, String listenerTag) {
        AlarmRecord record = new AlarmRecord();
        record.packageName = packageName != null ? packageName : sPackageName;
        record.type = type;
        record.triggerAt = triggerAt;
        record.interval = interval;
        record.operation = operation;
        record.listener = listener;
        record.listenerTag = listenerTag;
        synchronized (sAlarmLock) {
            sAlarms.add(record);
            scheduleAlarm(record);
        }
        log("alarm: set " + record.packageName + " type=" + type + " at=" + triggerAt
                + (interval > 0 ? " interval=" + interval : ""));
    }

    static void handleRemoveAlarm(Object operation, Object listener) {
        synchronized (sAlarmLock) {
            for (int i = sAlarms.size() - 1; i >= 0; i--) {
                AlarmRecord record = sAlarms.get(i);
                if ((operation != null && operation.equals(record.operation))
                        || (listener != null && listener.equals(record.listener))) {
                    if (record.future != null) {
                        record.future.cancel(false);
                    }
                    sAlarms.remove(i);
                }
            }
        }
        scheduleExitIfEmpty();
    }

    static void handleRemoveAllAlarms(String packageName) {
        synchronized (sAlarmLock) {
            for (int i = sAlarms.size() - 1; i >= 0; i--) {
                AlarmRecord record = sAlarms.get(i);
                if (packageName == null || packageName.equals(record.packageName)) {
                    if (record.future != null) {
                        record.future.cancel(false);
                    }
                    sAlarms.remove(i);
                }
            }
        }
        scheduleExitIfEmpty();
    }

    static boolean hasPendingWork() {
        synchronized (sAlarmLock) {
            if (!sAlarms.isEmpty()) {
                return true;
            }
        }
        if (hasPendingBroadcastWork()) {
            return true;
        }
        if (hasPendingJobs()) {
            return true;
        }
        return false;
    }

    static Object callNoThrow(Object target, String name) {
        if (target == null) {
            return null;
        }
        try {
            return target.getClass().getMethod(name).invoke(target);
        } catch (Throwable t) {
            return null;
        }
    }

    static int callInt(Object target, String name, int fallback) {
        Object value = callNoThrow(target, name);
        return value instanceof Number ? ((Number) value).intValue() : fallback;
    }

    static long callLong(Object target, String name, long fallback) {
        Object value = callNoThrow(target, name);
        return value instanceof Number ? ((Number) value).longValue() : fallback;
    }

    static boolean callBoolean(Object target, String name, boolean fallback) {
        Object value = callNoThrow(target, name);
        return value instanceof Boolean ? (Boolean) value : fallback;
    }

    static Object jobComponent(JobRecord record) {
        return callNoThrow(record.info, "getService");
    }

    static Object jobIntent(JobRecord record) throws Exception {
        Object component = jobComponent(record);
        Object intent = Class.forName("android.content.Intent").getDeclaredConstructor()
                .newInstance();
        intent.getClass().getMethod("setComponent",
                        Class.forName("android.content.ComponentName"))
                .invoke(intent, component);
        return intent;
    }

    static Object jobCallback(JobRecord record) throws Exception {
        Class<?> iface = Class.forName("android.app.job.IJobCallback");
        Object proxy = Proxy.newProxyInstance(iface.getClassLoader(), new Class<?>[] {iface},
                new InvocationHandler() {
                    @Override
                    public Object invoke(Object p, Method method, Object[] args) {
                        String name = method.getName();
                        if ("jobFinished".equals(name)) {
                            finishJob(record, args.length > 1 && (Boolean) args[1]);
                            return null;
                        }
                        if ("acknowledgeStartMessage".equals(name)) {
                            if (args.length > 1 && !(Boolean) args[1]) {
                                finishJob(record, false);
                            }
                            return null;
                        }
                        if ("acknowledgeStopMessage".equals(name)) {
                            finishJob(record, args.length > 1 && (Boolean) args[1]);
                            return null;
                        }
                        if ("dequeueWork".equals(name)) {
                            return null;
                        }
                        if ("completeWork".equals(name)) {
                            return true;
                        }
                        return defaultValue(method.getReturnType());
                    }
                });
        JobCallbackBinder binder = new JobCallbackBinder(record);
        binder.attachInterface((IInterface) proxy, "android.app.job.IJobCallback");
        return binder;
    }

    static Object makeJobParameters(JobRecord record) throws Exception {
        Object extras = callNoThrow(record.info, "getExtras");
        Object transientExtras = callNoThrow(record.info, "getTransientExtras");
        Object clipData = callNoThrow(record.info, "getClipData");
        int clipGrant = callInt(record.info, "getClipGrantFlags", 0);
        boolean expedited = callBoolean(record.info, "isExpedited", false);
        boolean userInitiated = callBoolean(record.info, "isUserInitiated", false);
        Object uris = callNoThrow(record.info, "getTriggeredContentUris");
        Object authorities = callNoThrow(record.info, "getTriggeredContentAuthorities");
        Object network = callNoThrow(record.info, "getRequiredNetwork");
        Object callback = jobCallback(record);
        Constructor<?> ctor = Class.forName("android.app.job.JobParameters").getConstructor(
                Class.forName("android.os.IBinder"), String.class, int.class,
                Class.forName("android.os.PersistableBundle"), Class.forName("android.os.Bundle"),
                Class.forName("android.content.ClipData"), int.class, boolean.class,
                boolean.class, boolean.class, Class.forName("[Landroid.net.Uri;"),
                Class.forName("[Ljava.lang.String;"), Class.forName("android.net.Network"));
        return ctor.newInstance(callback, record.namespace, record.jobId, extras,
                transientExtras, clipData, clipGrant, false, expedited, userInitiated, uris,
                authorities, network);
    }

    static void startJobWithBinder(JobRecord record, Object binder) {
        if (binder == null) {
            log("job " + record.jobId + ": null service binder");
            finishJob(record, false);
            return;
        }
        synchronized (sJobLock) {
            if (sJobs.get(record.jobId) != record) {
                return;
            }
            record.binder = binder;
        }
        try {
            Object params = makeJobParameters(record);
            record.params = params;
            Object iface = Class.forName("android.app.job.IJobService$Stub")
                    .getMethod("asInterface", Class.forName("android.os.IBinder"))
                    .invoke(null, binder);
            iface.getClass().getMethod("startJob",
                            Class.forName("android.app.job.JobParameters"))
                    .invoke(iface, params);
            log("job " + record.jobId + ": started");
        } catch (Throwable t) {
            log("job " + record.jobId + ": start failed: " + t);
            finishJob(record, false);
        }
    }

    static Object jobServiceConnection(JobRecord record) throws Exception {
        Class<?> iface = Class.forName("android.app.IServiceConnection");
        return Proxy.newProxyInstance(iface.getClassLoader(), new Class<?>[] {iface},
                new InvocationHandler() {
                    @Override
                    public Object invoke(Object p, Method method, Object[] args) {
                        String name = method.getName();
                        if ("connected".equals(name) && args != null && args.length > 1) {
                            startJobWithBinder(record, args[1]);
                        } else if ("died".equals(name)) {
                            finishJob(record, true);
                        }
                        return defaultValue(method.getReturnType());
                    }
                });
    }

    static void scheduleJob(JobRecord record) {
        long minLatency = callLong(record.info, "getMinLatencyMillis", 0);
        long maxDelay = callLong(record.info, "getMaxExecutionDelayMillis", 0);
        long delay = minLatency > 0 ? minLatency : maxDelay;
        if (delay < 0) {
            delay = 0;
        }
        record.periodic = callBoolean(record.info, "isPeriodic", false);
        record.interval = callLong(record.info, "getIntervalMillis", 0);
        record.future = sAlarmExecutor.schedule(new Runnable() {
            @Override
            public void run() {
                startJob(record);
            }
        }, delay, java.util.concurrent.TimeUnit.MILLISECONDS);
    }

    static void startJob(JobRecord record) {
        synchronized (sJobLock) {
            if (sJobs.get(record.jobId) != record || record.running) {
                return;
            }
            record.finished = false;
            record.running = true;
            sRunningJobs.put(record.jobId, record);
        }
        try {
            Object intent = jobIntent(record);
            Object info = resolveServiceInfo(intent);
            if (info == null) {
                log("job " + record.jobId + ": service not found");
                finishJob(record, false);
                return;
            }
            record.serviceInfo = info;
            Object connection = jobServiceConnection(record);
            record.connection = connection;
            if (handleBindService(intent, connection, 0x1, "job-" + record.jobId) == 0) {
                log("job " + record.jobId + ": bind failed");
                finishJob(record, false);
            }
        } catch (Throwable t) {
            log("job " + record.jobId + ": start failed: " + t);
            finishJob(record, false);
        }
    }

    static void finishJob(JobRecord record, boolean reschedule) {
        Object connection;
        Object binder;
        Object params;
        boolean periodic;
        long interval;
        synchronized (sJobLock) {
            if (record.finished) {
                return;
            }
            record.finished = true;
            if (record.future != null) {
                record.future.cancel(false);
                record.future = null;
            }
            sRunningJobs.remove(record.jobId);
            record.running = false;
            connection = record.connection;
            binder = record.binder;
            params = record.params;
            periodic = record.periodic;
            interval = record.interval;
            record.connection = null;
            record.binder = null;
            record.params = null;
            if (!reschedule && !periodic) {
                sJobs.remove(record.jobId);
            }
        }
        if (connection != null) {
            try {
                handleUnbindService(connection);
            } catch (Throwable t) {
                log("job " + record.jobId + ": unbind failed: " + t);
            }
        }
        log("job " + record.jobId + ": finished reschedule=" + reschedule);
        if (reschedule || periodic) {
            synchronized (sJobLock) {
                if (sJobs.get(record.jobId) == record) {
                    synchronized (sAlarmLock) {
                        record.future = sAlarmExecutor.schedule(new Runnable() {
                            @Override
                            public void run() {
                                startJob(record);
                            }
                        }, interval > 0 ? interval : 1000,
                                java.util.concurrent.TimeUnit.MILLISECONDS);
                    }
                }
            }
        }
        scheduleExitIfEmpty();
    }

    static void cancelJob(int jobId) {
        JobRecord record;
        Object params;
        synchronized (sJobLock) {
            record = sJobs.remove(jobId);
            if (record == null) {
                return;
            }
            record.finished = true;
            if (record.future != null) {
                record.future.cancel(false);
                record.future = null;
            }
            sRunningJobs.remove(jobId);
            params = record.params;
        }
        if (params != null && record.binder != null) {
            try {
                Object iface = Class.forName("android.app.job.IJobService$Stub")
                        .getMethod("asInterface", Class.forName("android.os.IBinder"))
                        .invoke(null, record.binder);
                iface.getClass().getMethod("stopJob",
                                Class.forName("android.app.job.JobParameters"))
                        .invoke(iface, params);
            } catch (Throwable t) {
                log("job " + jobId + ": stop failed: " + t);
            }
        }
        if (record.connection != null) {
            try {
                handleUnbindService(record.connection);
            } catch (Throwable t) {
                log("job " + jobId + ": unbind failed: " + t);
            }
        }
        log("job " + jobId + ": cancelled");
        scheduleExitIfEmpty();
    }

    static Object jobList() {
        ArrayList<Object> jobs = new ArrayList<>();
        synchronized (sJobLock) {
            for (JobRecord record : sJobs.values()) {
                jobs.add(record.info);
            }
        }
        return parceledList(jobs);
    }

    static Object jobMap() {
        HashMap<String, Object> map = new HashMap<>();
        synchronized (sJobLock) {
            HashMap<String, ArrayList<Object>> byNamespace = new HashMap<>();
            for (JobRecord record : sJobs.values()) {
                String ns = record.namespace != null ? record.namespace : "";
                ArrayList<Object> list = byNamespace.get(ns);
                if (list == null) {
                    list = new ArrayList<>();
                    byNamespace.put(ns, list);
                }
                list.add(record.info);
            }
            for (Map.Entry<String, ArrayList<Object>> entry : byNamespace.entrySet()) {
                map.put(entry.getKey(), parceledList(entry.getValue()));
            }
        }
        return map;
    }

    static boolean hasPendingJobs() {
        synchronized (sJobLock) {
            return !sJobs.isEmpty() || !sRunningJobs.isEmpty();
        }
    }

    static void scheduleNewJob(String namespace, Object info) {
        int jobId = callInt(info, "getId", 0);
        JobRecord record;
        synchronized (sJobLock) {
            JobRecord old = sJobs.remove(jobId);
            if (old != null && old.future != null) {
                old.future.cancel(false);
            }
            record = new JobRecord();
            record.jobId = jobId;
            record.finished = false;
            record.namespace = namespace;
            record.info = info;
            sJobs.put(jobId, record);
            scheduleJob(record);
        }
        log("job " + jobId + ": scheduled namespace=" + namespace);
    }

}
