/*
 * App-defined service management for DroidletLauncher.
 *
 * Services belong to the launched package: start/bind/stop resolve manifest
 * components, create the service through ActivityThread's real code path and
 * keep the returned binder in-process.  A manifest service with
 * android:process is launched as a separate service-host process; in that mode
 * this file also provides the child side (ServiceHostRoot) that publishes
 * onBind() over binder RPC and accepts start/stop from the parent.
 */

package android.app;

import android.content.Intent;
import android.os.Binder;
import android.os.IBinder;
import android.os.Parcel;
import android.os.RemoteException;

import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Map;

import static android.app.DroidletLauncher.*;
import static android.app.DroidletLauncherBroadcasts.*;
import static android.app.DroidletLauncherStartup.*;

final class DroidletLauncherServices {
    private DroidletLauncherServices() {}

    // App-defined services.  Only components of the launched package are
    // accepted; there is no cross-package service registry.
    static final Object sServiceLock = new Object();
    static final Map<String, ServiceRecord> sServiceByComponent = new HashMap<>();
    static final Map<Object, ServiceRecord> sServiceByToken = new IdentityHashMap<>();
    static final Map<Object, ConnectionRecord> sServiceByBindToken = new IdentityHashMap<>();
    static final Map<Object, ConnectionRecord> sServiceByConnection = new IdentityHashMap<>();
    static int sNextServiceStartId = 1;
    static long sNextBindSeq = 1;

    // Service-host mode (child side): the record for the service this process
    // runs, and the private transactions the parent uses to drive it.
    static ServiceRecord sServiceHostRecord;
    static final int HOST_STOP_TRANSACTION = 0x7fffff01;
    static final int HOST_START_TRANSACTION = 0x7fffff02;
    static final int HOST_STOP_STARTED_TRANSACTION = 0x7fffff03;

    static void launchServiceHostThread() throws Exception {
        if (sServiceRpcFd < 0) {
            throw new IllegalArgumentException("service host requires --rpc-fd");
        }
        log("starting service host for " + sPackageName + "/" + sClassName
                + " process=" + sProcessName);
        launchActivityThread();
    }

    static void postServiceHostStart(Object thread) throws Exception {
        Object handler = Class.forName("android.app.ActivityThread")
                .getMethod("getHandler").invoke(thread);
        Runnable start = new Runnable() {
            @Override
            public void run() {
                try {
                    startServiceHost();
                } catch (Throwable t) {
                    log("service host: failed to start service: " + t);
                    t.printStackTrace(System.err);
                    System.exit(1);
                }
            }
        };
        Class.forName("android.os.Handler")
                .getMethod("post", Runnable.class)
                .invoke(handler, start);
    }

    static void startServiceHost() throws Exception {
        Object info = sServiceInfo;
        if (info == null) {
            throw new IllegalStateException("service info was not resolved");
        }
        setField(info, "applicationInfo", sAppInfo);

        ServiceRecord record = getOrCreateServiceRecord(info);
        sServiceHostRecord = record;
        if (!createServiceRecord(record)) {
            throw new IllegalStateException("unable to create service " + record.className);
        }

        ConnectionRecord binding = new ConnectionRecord();
        binding.service = record;
        binding.connection = null;
        binding.bindToken = newBinder();
        binding.intent = newIntentForComponent(record.packageName, record.className);
        binding.flags = 0x1; // Context.BIND_AUTO_CREATE
        binding.internal = true;
        synchronized (sServiceLock) {
            record.bindings.add(binding);
            sServiceByBindToken.put(binding.bindToken, binding);
        }
        scheduleBindService(record, binding, false);
        log("service host: binding " + record.className);
    }

    static void serviceHostReady(Object root) {
        try {
            boolean ready = nativeServiceHostSetRoot(root, sServiceRpcFd);
            if (!ready) {
                log("service host: unable to publish binder");
                System.exit(1);
            }
            log("service host: ready");
        } catch (Throwable t) {
            log("service host: publish failed: " + t);
            t.printStackTrace(System.err);
            System.exit(1);
        }
    }

    static void serviceHostStopRequested() {
        postToActivityThread(new Runnable() {
            @Override
            public void run() {
                stopServiceHost();
            }
        });
    }

    static void scheduleServiceHostStart(Object intent, int startId, int flags,
            boolean foreground) {
        ServiceRecord record = sServiceHostRecord;
        if (record == null) {
            return;
        }
        record.started = true;
        record.lastStartId = startId;
        record.lastFlags = flags;
        record.lastIntent = intent;
        if (foreground) {
            record.foreground = true;
        }
        scheduleServiceArgs(record, startId, flags, intent);
        log("service host: onStartCommand startId=" + startId
                + (foreground ? " fg" : ""));
    }

    static void stopServiceHostStarted() {
        ServiceRecord record = sServiceHostRecord;
        if (record == null) {
            return;
        }
        record.started = false;
        record.foreground = false;
        log("service host: stopped started state");
        scheduleExitIfEmpty();
    }

    static void stopServiceHost() {
        ServiceRecord record = sServiceHostRecord;
        if (record == null) {
            return;
        }
        ConnectionRecord binding = null;
        synchronized (sServiceLock) {
            if (!record.bindings.isEmpty()) {
                binding = record.bindings.get(0);
            }
            for (ConnectionRecord candidate : record.bindings) {
                sServiceByBindToken.remove(candidate.bindToken);
                sServiceByConnection.remove(candidate.connection);
            }
            record.bindings.clear();
        }
        if (binding != null && record.token != null) {
            try {
                appThreadInvoke("scheduleUnbindService",
                        new Class<?>[] {
                                Class.forName("android.os.IBinder"),
                                Class.forName("android.os.IBinder"),
                                Class.forName("android.content.Intent")},
                        record.token, binding.bindToken, binding.intent);
            } catch (Throwable t) {
                log("service host: unbind failed: " + t);
            }
        }
        if (record.token != null) {
            try {
                appThreadInvoke("scheduleStopService",
                        new Class<?>[] {Class.forName("android.os.IBinder")},
                        record.token);
            } catch (Throwable t) {
                log("service host: stop failed: " + t);
            }
        }
        log("service host: stop requested");
    }

    static Object newIntentForComponent(String packageName, String className)
            throws Exception {
        Object intent = Class.forName("android.content.Intent").getDeclaredConstructor()
                .newInstance();
        Object component = componentName(packageName, className);
        intent.getClass().getMethod("setComponent",
                        Class.forName("android.content.ComponentName"))
                .invoke(intent, component);
        return intent;
    }

    static final class ServiceRecord {
        Object token;
        Object info;
        String packageName;
        String className;
        boolean created;
        boolean started;
        int lastStartId;
        Object lastIntent;
        int lastFlags;
        boolean foreground;
        boolean remote;
        boolean cancelled;
        Object binder;
        final ArrayList<ConnectionRecord> bindings = new ArrayList<>();
    }

    static final class ConnectionRecord {
        ServiceRecord service;
        Object connection;
        Object bindToken;
        Object intent;
        int flags;
        String instanceName;
        boolean connected;
        boolean internal;
    }

    static Object componentName(String packageName, String className)
            throws Exception {
        return Class.forName("android.content.ComponentName")
                .getConstructor(String.class, String.class)
                .newInstance(packageName, className);
    }

    static Object resolveServiceInfo(Object intent) throws Exception {
        if (sParsedPackage == null || intent == null) {
            return null;
        }
        Object component = intent.getClass().getMethod("getComponent").invoke(intent);
        Object service = null;
        if (component != null) {
            String pkg = componentField(component, "getPackageName");
            String cls = componentField(component, "getClassName");
            if (pkg != null && !pkg.equals(sPackageName)) {
                log("service: refusing cross-package component " + pkg + "/" + cls);
                return null;
            }
            service = findParsedService(sParsedPackage, cls);
            if (service == null) {
                log("service: not declared in manifest: " + cls);
                return null;
            }
        } else {
            List<?> services = (List<?>) getFieldValue(sParsedPackage, "services");
            for (Object candidate : services) {
                List<?> filters = (List<?>) getFieldValue(candidate, "intents");
                for (Object filter : filters) {
                    if (intentFilterMatches(filter, intent)) {
                        service = candidate;
                        break;
                    }
                }
                if (service != null) {
                    break;
                }
            }
            if (service == null) {
                log("service: no in-package match for implicit intent");
                return null;
            }
        }
        return generateComponentInfo("Service", service, 0);
    }

    static boolean isSeparateProcessService(Object info) throws Exception {
        String processName = (String) getFieldValue(info, "processName");
        int flags = (Integer) getFieldValue(info, "flags");
        int isolated;
        try {
            isolated = staticInt(Class.forName("android.content.pm.ServiceInfo"),
                    "FLAG_ISOLATED_PROCESS", 0x2);
        } catch (Throwable ignored) {
            isolated = 0x2;
        }
        boolean separateProcess = processName != null && !processName.isEmpty()
                && !processName.equals(sPackageName);
        return separateProcess || (flags & isolated) != 0;
    }

    static ServiceRecord getOrCreateServiceRecord(Object info) throws Exception {
        String packageName = (String) getFieldValue(info, "packageName");
        if (packageName == null || packageName.isEmpty()) {
            packageName = sPackageName;
        }
        String className = (String) getFieldValue(info, "name");
        String key = packageName + "/" + className;
        synchronized (sServiceLock) {
            ServiceRecord record = sServiceByComponent.get(key);
            if (record == null) {
                record = new ServiceRecord();
                record.info = info;
                record.packageName = packageName;
                record.className = className;
                sServiceByComponent.put(key, record);
            }
            return record;
        }
    }

    static boolean createServiceRecord(ServiceRecord record) {
        if (record.created) {
            return true;
        }
        try {
            record.token = newBinder();
            record.created = true;
            synchronized (sServiceLock) {
                sServiceByToken.put(record.token, record);
            }
            appThreadInvoke("scheduleCreateService",
                    new Class<?>[] {
                            Class.forName("android.os.IBinder"),
                            Class.forName("android.content.pm.ServiceInfo"),
                            Class.forName("android.content.res.CompatibilityInfo"),
                            int.class},
                    record.token, record.info, defaultCompatibilityInfo(),
                    serviceProcessState());
            log("service: create " + record.className);
            return true;
        } catch (Throwable t) {
            record.created = false;
            log("service: create failed for " + record.className + ": " + t);
            return false;
        }
    }

    static void scheduleServiceArgs(ServiceRecord record, int startId,
            int flags, Object intent) {
        try {
            Class<?> startArgsClass = Class.forName("android.app.ServiceStartArgs");
            Object startArgs = startArgsClass.getConstructor(
                            boolean.class, int.class, int.class,
                            Class.forName("android.content.Intent"))
                    .newInstance(false, startId, flags, intent);
            ArrayList<Object> args = new ArrayList<>();
            args.add(startArgs);
            appThreadInvoke("scheduleServiceArgs",
                    new Class<?>[] {
                            Class.forName("android.os.IBinder"),
                            Class.forName("android.content.pm.ParceledListSlice")},
                    record.token, parceledList(args));
        } catch (Throwable t) {
            log("service: start args failed for " + record.className + ": " + t);
        }
    }

    static void scheduleBindService(ServiceRecord record, ConnectionRecord connection,
            boolean rebind) {
        try {
            long seq;
            synchronized (sServiceLock) {
                seq = sNextBindSeq++;
            }
            appThreadInvoke("scheduleBindService",
                    new Class<?>[] {
                            Class.forName("android.os.IBinder"),
                            Class.forName("android.os.IBinder"),
                            Class.forName("android.content.Intent"),
                            boolean.class,
                            int.class,
                            long.class},
                    record.token, connection.bindToken, connection.intent, rebind,
                    serviceProcessState(), seq);
        } catch (Throwable t) {
            log("service: bind failed for " + record.className + ": " + t);
        }
    }

    static void notifyServiceConnected(ServiceRecord record,
            ConnectionRecord connection, Object binder) {
        if (connection == null || connection.connection == null) {
            return;
        }
        if (connection.connected) {
            return;
        }
        connection.connected = true;
        try {
            Class<?> iface = Class.forName("android.app.IServiceConnection");
            Class<?> componentClass = Class.forName("android.content.ComponentName");
            Class<?> binderClass = Class.forName("android.os.IBinder");
            Method connected;
            Class<?> sessionClass = null;
            try {
                sessionClass = Class.forName("android.app.IBinderSession");
            } catch (Throwable ignored) {
            }
            if (sessionClass != null) {
                connected = iface.getMethod("connected", componentClass, binderClass,
                        sessionClass, boolean.class);
                connected.invoke(connection.connection,
                        componentName(record.packageName, record.className), binder,
                        null, false);
            } else {
                connected = iface.getMethod("connected", componentClass, binderClass,
                        boolean.class);
                connected.invoke(connection.connection,
                        componentName(record.packageName, record.className), binder,
                        false);
            }
            log("service: connected " + record.className);
        } catch (Throwable t) {
            log("service: connected callback failed for " + record.className + ": " + t);
        }
    }

    static boolean hasExternalBindings(ServiceRecord record) {
        for (ConnectionRecord binding : record.bindings) {
            if (!binding.internal) {
                return true;
            }
        }
        return false;
    }

    static boolean stopServiceRecord(ServiceRecord record) {
        record.started = false;
        record.foreground = false;
        if (record.remote) {
            Object binder;
            boolean stopHost;
            synchronized (sServiceLock) {
                binder = record.binder;
                stopHost = !hasExternalBindings(record);
                if (stopHost) {
                    record.cancelled = true;
                    record.remote = false;
                    record.binder = null;
                }
            }
            if (binder != null) {
                if (stopHost) {
                    nativeStopServiceHost(binder);
                } else {
                    sendServiceHostStopStarted(binder);
                }
            }
            if (stopHost) {
                removeServiceRecord(record);
            }
            scheduleExitIfEmpty();
            return true;
        }
        if (!hasExternalBindings(record) && record.token != null) {
            try {
                appThreadInvoke("scheduleStopService",
                        new Class<?>[] {Class.forName("android.os.IBinder")},
                        record.token);
                log("service: stop " + record.className);
            } catch (Throwable t) {
                log("service: stop failed for " + record.className + ": " + t);
            }
        }
        scheduleExitIfEmpty();
        return true;
    }

    static boolean hasActiveServices() {
        synchronized (sServiceLock) {
            for (ServiceRecord record : sServiceByComponent.values()) {
                if (record.started || record.foreground || hasExternalBindings(record)) {
                    return true;
                }
            }
        }
        return false;
    }

    static void removeServiceRecord(ServiceRecord record) {
        synchronized (sServiceLock) {
            String key = record.packageName + "/" + record.className;
            if (sServiceByComponent.get(key) != record) {
                return;
            }
            for (ConnectionRecord connection : record.bindings) {
                sServiceByBindToken.remove(connection.bindToken);
                sServiceByConnection.remove(connection.connection);
            }
            record.bindings.clear();
            sServiceByComponent.remove(key);
            if (record.token != null) {
                sServiceByToken.remove(record.token);
            }
            record.token = null;
            record.created = false;
            record.binder = null;
        }
    }

    static int intentFlags(Object intent) {
        try {
            return (Integer) intent.getClass().getMethod("getFlags").invoke(intent);
        } catch (Throwable t) {
            return 0;
        }
    }

    static ServiceRecord findServiceRecord(Object info) throws Exception {
        String packageName = (String) getFieldValue(info, "packageName");
        if (packageName == null || packageName.isEmpty()) {
            packageName = sPackageName;
        }
        String className = (String) getFieldValue(info, "name");
        synchronized (sServiceLock) {
            return sServiceByComponent.get(packageName + "/" + className);
        }
    }

    static Object handleStartService(Object intent, boolean requireForeground)
            throws Exception {
        Object info = resolveServiceInfo(intent);
        if (info == null) {
            return null;
        }
        if (isSeparateProcessService(info)) {
            return handleStartServiceHost(info, intent, requireForeground);
        }
        ServiceRecord record = getOrCreateServiceRecord(info);
        if (!createServiceRecord(record)) {
            return null;
        }
        record.started = true;
        synchronized (sServiceLock) {
            record.lastStartId = sNextServiceStartId++;
        }
        record.lastIntent = intent;
        record.lastFlags = intentFlags(intent);
        if (requireForeground) {
            record.foreground = true;
        }
        scheduleServiceArgs(record, record.lastStartId, record.lastFlags, intent);
        log("service: start " + record.className + " startId=" + record.lastStartId
                + (requireForeground ? " fg" : ""));
        return componentName(record.packageName, record.className);
    }

    static int handleStopService(Object intent) throws Exception {
        Object info = resolveServiceInfo(intent);
        if (info == null) {
            return 0;
        }
        ServiceRecord record = findServiceRecord(info);
        if (record == null) {
            return 0;
        }
        return stopServiceRecord(record) ? 1 : 0;
    }

    static boolean handleStopServiceToken(Object componentName, Object token,
            int startId) throws Exception {
        ServiceRecord record = null;
        synchronized (sServiceLock) {
            if (token != null) {
                record = sServiceByToken.get(token);
            }
            if (record == null && componentName != null) {
                String pkg = componentField(componentName, "getPackageName");
                String cls = componentField(componentName, "getClassName");
                record = sServiceByComponent.get(pkg + "/" + cls);
            }
        }
        if (record == null) {
            return false;
        }
        if (startId != -1 && startId != record.lastStartId) {
            return false;
        }
        return stopServiceRecord(record);
    }

    static int handleBindService(Object intent, Object connection, int flags,
            String instanceName) throws Exception {
        if (connection == null) {
            return 0;
        }
        Object info = resolveServiceInfo(intent);
        if (info == null) {
            return 0;
        }
        if (isSeparateProcessService(info)) {
            return handleBindServiceHost(info, intent, connection, flags, instanceName);
        }
        ServiceRecord record = getOrCreateServiceRecord(info);
        boolean autoCreate = (flags & 0x1) != 0; // Context.BIND_AUTO_CREATE
        if (!record.created) {
            if (!autoCreate || !createServiceRecord(record)) {
                return 0;
            }
        }
        ConnectionRecord binding = new ConnectionRecord();
        binding.service = record;
        binding.connection = connection;
        binding.bindToken = newBinder();
        binding.intent = intent;
        binding.flags = flags;
        binding.instanceName = instanceName;
        synchronized (sServiceLock) {
            record.bindings.add(binding);
            sServiceByBindToken.put(binding.bindToken, binding);
            sServiceByConnection.put(connection, binding);
        }
        if (record.binder != null) {
            notifyServiceConnected(record, binding, record.binder);
        } else {
            scheduleBindService(record, binding, false);
        }
        log("service: bind " + record.className
                + (instanceName != null ? " instance=" + instanceName : ""));
        return 1;
    }

    static int handleBindServiceHost(final Object info, Object intent, Object connection,
            int flags, String instanceName) throws Exception {
        final ServiceRecord record = getOrCreateServiceRecord(info);
        ConnectionRecord binding = new ConnectionRecord();
        binding.service = record;
        binding.connection = connection;
        binding.bindToken = newBinder();
        binding.intent = intent;
        binding.flags = flags;
        binding.instanceName = instanceName;
        final boolean startHost;
        final Object connectedBinder;
        synchronized (sServiceLock) {
            record.bindings.add(binding);
            sServiceByBindToken.put(binding.bindToken, binding);
            sServiceByConnection.put(connection, binding);
            connectedBinder = record.binder;
            // Only the first binding starts the child.  Later bindings wait
            // for the same RPC root instead of spawning another process.
            startHost = !record.remote;
            if (startHost) {
                record.remote = true;
                record.cancelled = false;
            }
        }
        log("service: bind (host) " + record.className
                + (instanceName != null ? " instance=" + instanceName : ""));
        if (!startHost) {
            if (connectedBinder != null) {
                notifyServiceConnected(record, binding, connectedBinder);
            }
            return 1;
        }

        startRemoteServiceHost(record);
        return 1;
    }

    static Object handleStartServiceHost(Object info, Object intent,
            boolean requireForeground) throws Exception {
        final ServiceRecord record = getOrCreateServiceRecord(info);
        final Object binder;
        final boolean startHost;
        synchronized (sServiceLock) {
            record.started = true;
            record.lastStartId = sNextServiceStartId++;
            record.lastIntent = intent;
            record.lastFlags = intentFlags(intent);
            if (requireForeground) {
                record.foreground = true;
            }
            binder = record.binder;
            startHost = !record.remote;
            if (startHost) {
                record.remote = true;
                record.cancelled = false;
            }
        }
        log("service: start (host) " + record.className + " startId=" + record.lastStartId
                + (requireForeground ? " fg" : ""));
        if (startHost) {
            startRemoteServiceHost(record);
        } else if (binder != null) {
            sendServiceHostStart(binder, record.lastIntent, record.lastStartId,
                    record.lastFlags, record.foreground);
        }
        return componentName(record.packageName, record.className);
    }

    static void startRemoteServiceHost(final ServiceRecord record) {
        Thread thread = new Thread(new Runnable() {
            @Override
            public void run() {
                Object binder = null;
                try {
                    binder = nativeStartServiceHost(sApkPath, sDataDir,
                            record.packageName + "/" + record.className);
                } catch (Throwable t) {
                    log("service: host start failed for " + record.className + ": " + t);
                    t.printStackTrace(System.err);
                }

                ArrayList<ConnectionRecord> pending = new ArrayList<>();
                Object startIntent = null;
                int startId = 0;
                int startFlags = 0;
                boolean foreground = false;
                synchronized (sServiceLock) {
                    if (record.cancelled) {
                        if (binder != null) {
                            nativeStopServiceHost(binder);
                        }
                        return;
                    }
                    record.binder = binder;
                    pending.addAll(record.bindings);
                    if (record.started) {
                        startIntent = record.lastIntent;
                        startId = record.lastStartId;
                        startFlags = record.lastFlags;
                        foreground = record.foreground;
                    }
                }
                if (binder == null) {
                    log("service: host unavailable for " + record.className);
                    synchronized (sServiceLock) {
                        record.remote = false;
                    }
                    return;
                }
                log("service: host ready " + record.className);
                if (startIntent != null) {
                    sendServiceHostStart(binder, startIntent, startId, startFlags, foreground);
                }
                for (ConnectionRecord candidate : pending) {
                    notifyServiceConnected(record, candidate, binder);
                }
                startServiceHostMonitor(record, binder);
            }
        }, "service-host-" + record.className);
        thread.setDaemon(true);
        thread.start();
    }

    static void startServiceHostMonitor(final ServiceRecord record, final Object binder) {
        Thread monitor = new Thread(new Runnable() {
            @Override
            public void run() {
                try {
                    while (nativeIsServiceHostAlive(binder)) {
                        Thread.sleep(200);
                    }
                } catch (InterruptedException ignored) {
                    return;
                } catch (Throwable t) {
                    log("service: host monitor failed: " + t);
                }
                handleServiceHostExited(record, binder);
            }
        }, "service-host-monitor-" + record.className);
        monitor.setDaemon(true);
        monitor.start();
    }

    static void handleServiceHostExited(ServiceRecord record, Object binder) {
        synchronized (sServiceLock) {
            if (record.cancelled || record.binder != binder) {
                return;
            }
            record.cancelled = true;
            record.started = false;
            record.foreground = false;
            record.remote = false;
            record.binder = null;
        }
        log("service: host exited " + record.className);
        try {
            nativeStopServiceHost(binder);
        } catch (Throwable t) {
            log("service: host cleanup failed: " + t);
        }
        removeServiceRecord(record);
        scheduleExitIfEmpty();
    }

    static void sendServiceHostStart(Object binder, Object intent, int startId,
            int flags, boolean foreground) {
        if (binder == null || intent == null) {
            return;
        }
        try {
            Parcel data = Parcel.obtain((IBinder) binder);
            ((Intent) intent).writeToParcel(data, 0);
            data.writeInt(startId);
            data.writeInt(flags);
            data.writeInt(foreground ? 1 : 0);
            ((IBinder) binder).transact(HOST_START_TRANSACTION, data, null, 1);
            data.recycle();
        } catch (Throwable t) {
            log("service: start command delivery failed: " + t);
        }
    }

    static void sendServiceHostStopStarted(Object binder) {
        if (binder == null) {
            return;
        }
        try {
            Parcel data = Parcel.obtain((IBinder) binder);
            ((IBinder) binder).transact(HOST_STOP_STARTED_TRANSACTION, data, null, 1);
            data.recycle();
        } catch (Throwable t) {
            log("service: stop-started delivery failed: " + t);
        }
    }

    static boolean handleUnbindService(Object connection) throws Exception {
        ConnectionRecord binding;
        synchronized (sServiceLock) {
            binding = sServiceByConnection.remove(connection);
            if (binding == null) {
                return false;
            }
            binding.service.bindings.remove(binding);
            sServiceByBindToken.remove(binding.bindToken);
        }
        ServiceRecord record = binding.service;
        if (record.remote) {
            Object binder = null;
            boolean lastBinding;
            boolean keepHost;
            synchronized (sServiceLock) {
                lastBinding = !hasExternalBindings(record);
                keepHost = record.started || record.foreground;
                if (lastBinding && !keepHost) {
                    record.cancelled = true;
                    record.remote = false;
                    binder = record.binder;
                    record.binder = null;
                }
            }
            if (!lastBinding || keepHost) {
                return true;
            }
            if (binder != null) {
                nativeStopServiceHost(binder);
            }
            synchronized (sServiceLock) {
                // A new binding may have arrived while the old host was
                // stopping; keep the record in that case.
                if (!record.bindings.isEmpty() || record.remote) {
                    return true;
                }
            }
            log("service: destroy host " + record.className);
            removeServiceRecord(record);
            return true;
        }
        try {
            appThreadInvoke("scheduleUnbindService",
                    new Class<?>[] {
                            Class.forName("android.os.IBinder"),
                            Class.forName("android.os.IBinder"),
                            Class.forName("android.content.Intent")},
                    record.token, binding.bindToken, binding.intent);
        } catch (Throwable t) {
            log("service: unbind failed for " + record.className + ": " + t);
        }
        log("service: unbind " + record.className);
        if (!record.started && !hasExternalBindings(record) && record.token != null) {
            try {
                appThreadInvoke("scheduleStopService",
                        new Class<?>[] {Class.forName("android.os.IBinder")},
                        record.token);
            } catch (Throwable t) {
                log("service: stop after unbind failed for " + record.className + ": " + t);
            }
            scheduleExitIfEmpty();
        }
        return true;
    }

    static void handlePublishService(Object token, Object bindToken, Object binder) {
        ServiceRecord record;
        ConnectionRecord binding;
        synchronized (sServiceLock) {
            record = sServiceByToken.get(token);
            binding = sServiceByBindToken.get(bindToken);
        }
        if (record == null) {
            return;
        }
        record.binder = binder;
        if (binding != null) {
            notifyServiceConnected(record, binding, binder);
        }
        if (sServiceComponent != null && record == sServiceHostRecord) {
            serviceHostReady(new ServiceHostRoot(binder));
        }
    }

    static void handleServiceDoneExecuting(Object token, int type, int startId,
            int result) {
        if (type == 2) { // ActivityThread.SERVICE_DONE_EXECUTING_STOP
            ServiceRecord record;
            synchronized (sServiceLock) {
                record = sServiceByToken.get(token);
            }
            if (record != null) {
                log("service: destroyed " + record.className);
                removeServiceRecord(record);
            }
            scheduleExitIfEmpty();
        } else if (type == 1) { // SERVICE_DONE_EXECUTING_START
            log("service: onStartCommand result=" + result + " startId=" + startId);
        }
    }

    static void handleSetServiceForeground(Object componentName, Object token,
            int id, Object notification) {
        ServiceRecord record = null;
        synchronized (sServiceLock) {
            if (token != null) {
                record = sServiceByToken.get(token);
            }
            if (record == null && componentName != null) {
                String pkg = componentField(componentName, "getPackageName");
                String cls = componentField(componentName, "getClassName");
                record = sServiceByComponent.get(pkg + "/" + cls);
            }
        }
        if (record != null) {
            record.foreground = id != 0 && notification != null;
            log("service: foreground " + record.className + "=" + record.foreground);
        }
    }

    static Object handlePeekService(Object intent) throws Exception {
        Object info = resolveServiceInfo(intent);
        if (info == null) {
            return null;
        }
        ServiceRecord record = findServiceRecord(info);
        return record != null ? record.binder : null;
    }

    static Object intentFromParcel(Parcel data) {
        try {
            Class<?> intentClass = Class.forName("android.content.Intent");
            Object creator = intentClass.getField("CREATOR").get(null);
            return creator.getClass()
                    .getMethod("createFromParcel", Class.forName("android.os.Parcel"))
                    .invoke(creator, data);
        } catch (Throwable t) {
            log("service host: unable to read start intent: " + t);
            return null;
        }
    }

    static final class ServiceHostRoot extends Binder {
        private final Object mTarget;

        ServiceHostRoot(Object target) {
            mTarget = target;
        }

        @Override
        protected boolean onTransact(int code, Parcel data, Parcel reply, int flags)
                throws RemoteException {
            if (System.getenv("DROIDLET_LOG_SERVICE_RPC") != null) {
                log("service host: transact code=" + code);
            }
            if (code == HOST_STOP_TRANSACTION) {
                postToActivityThread(new Runnable() {
                    @Override
                    public void run() {
                        stopServiceHost();
                    }
                });
                return true;
            }
            if (code == HOST_START_TRANSACTION) {
                final Object intent = intentFromParcel(data);
                final int startId = data.readInt();
                final int startFlags = data.readInt();
                final boolean foreground = data.readInt() != 0;
                postToActivityThread(new Runnable() {
                    @Override
                    public void run() {
                        scheduleServiceHostStart(intent, startId, startFlags, foreground);
                    }
                });
                return true;
            }
            if (code == HOST_STOP_STARTED_TRANSACTION) {
                postToActivityThread(new Runnable() {
                    @Override
                    public void run() {
                        stopServiceHostStarted();
                    }
                });
                return true;
            }
            if (mTarget == null) {
                return false;
            }
            return ((IBinder) mTarget).transact(code, data, reply, flags);
        }
    }

}
