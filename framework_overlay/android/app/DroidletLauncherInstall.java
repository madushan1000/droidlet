/*
 * APK parsing and installation for DroidletLauncher.
 *
 * The launcher parses the APK with the framework's own PackageParser, extracts
 * the x86_64 native libraries into $ANDROID_DATA/app/<package>, picks the
 * launched or service-host component from the manifest, and installs the host
 * external-storage view.  No system server is involved.
 */

package android.app;

import java.io.File;
import java.io.InputStream;
import java.lang.reflect.Field;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.Enumeration;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

import static android.app.DroidletLauncher.*;
import static android.app.DroidletLauncherServices.*;
import static android.app.DroidletLauncherSystemServices.*;
import static android.app.DroidletLauncherStartup.*;

final class DroidletLauncherInstall {
    private DroidletLauncherInstall() {}

    static final class ManifestInfo {
        String packageName;
        String mainActivity;
        String applicationClass;
        String label;
        int applicationTheme;
        int mainActivityTheme;
        int targetSdk;
        int minSdk;
    }

    static String resolveClassName(String pkg, String name) {
        if (name.startsWith(".")) {
            return pkg + name;
        }
        if (name.indexOf('.') < 0) {
            return pkg + "." + name;
        }
        return name;
    }

    static Object parsePackageWithPackageParser(String apkPath) throws Exception {
        try {
            Class<?> parserClass = Class.forName("android.content.pm.PackageParser");
            Object parser = parserClass.getDeclaredConstructor().newInstance();
            Method parse = parserClass.getMethod(
                    "parseMonolithicPackage", File.class, int.class);
            Object pkg = parse.invoke(parser, new File(apkPath), 0 /* flags */);
            // PackageParser only records signatures in a separate pass.  Without
            // it PackageInfo.signingInfo is null and apps that read it (ACRA in
            // NewPipe, F-Droid's own update check) crash or report "no
            // signatures".  skipVerify=true parses the v1/v2/v3 certificates
            // without doing the digest checks against the APK contents.
            try {
                Class<?> pkgClass = Class.forName("android.content.pm.PackageParser$Package");
                Method collect = parserClass.getMethod("collectCertificates", pkgClass,
                                                       boolean.class);
                collect.invoke(null, pkg, true);
            } catch (Throwable t) {
                log("unable to collect APK certificates: " + t);
            }
            log("PackageParser parsed " + getFieldValue(pkg, "packageName"));
            return pkg;
        } catch (InvocationTargetException e) {
            Throwable cause = e.getCause();
            throw new IllegalArgumentException(
                    "PackageParser could not parse " + apkPath, cause);
        } catch (Throwable t) {
            throw new IllegalArgumentException(
                    "PackageParser unavailable for " + apkPath, t);
        }
    }

    static ManifestInfo manifestInfoFromPackage(Object pkg) throws Exception {
        ManifestInfo info = new ManifestInfo();
        info.packageName = (String) getFieldValue(pkg, "packageName");

        Object appInfo = getFieldValue(pkg, "applicationInfo");
        if (appInfo != null) {
            info.applicationClass = (String) getFieldValue(appInfo, "className");
            info.applicationTheme = (Integer) getFieldValue(appInfo, "theme");
            info.targetSdk = (Integer) getFieldValue(appInfo, "targetSdkVersion");
            info.minSdk = (Integer) getFieldValue(appInfo, "minSdkVersion");
            Object label = getFieldValue(appInfo, "nonLocalizedLabel");
            if (label != null) {
                info.label = String.valueOf(label);
            }
        }

        Object launcher = findLauncherActivity(pkg);
        if (launcher != null) {
            info.mainActivity = (String) getFieldValue(launcher, "name");
            info.mainActivityTheme = (Integer) getFieldValue(launcher, "theme");
        }
        return info;
    }

    static Object findLauncherActivity(Object pkg) throws Exception {
        List<?> activities = (List<?>) getFieldValue(pkg, "activities");
        for (Object activity : activities) {
            List<?> intents = (List<?>) getFieldValue(activity, "intents");
            for (Object intent : intents) {
                if (hasIntentActionOrCategory(intent, "android.intent.action.MAIN")
                        && hasIntentActionOrCategory(
                                intent, "android.intent.category.LAUNCHER")) {
                    return getFieldValue(activity, "info");
                }
            }
        }
        return null;
    }

    static boolean hasIntentActionOrCategory(Object intent, String value) {
        try {
            return (Boolean) intent.getClass().getMethod("hasAction", String.class)
                    .invoke(intent, value)
                    || (Boolean) intent.getClass().getMethod("hasCategory", String.class)
                            .invoke(intent, value);
        } catch (Throwable t) {
            return false;
        }
    }

    static boolean hasSystemFeature(String feature) {
        if (feature == null) {
            return false;
        }
        switch (feature) {
            case "android.hardware.touchscreen":
            case "android.hardware.touchscreen.multitouch":
            case "android.hardware.touchscreen.multitouch.distinct":
            case "android.hardware.faketouch":
            case "android.hardware.faketouch.multitouch.distinct":
            case "android.hardware.screen.landscape":
            case "android.hardware.screen.portrait":
            case "android.hardware.audio.output":
            case "android.hardware.audio.low_latency":
            case "android.hardware.ram.normal":
            case "android.software.app_widgets":
            case "android.software.backup":
            case "android.software.home_screen":
            case "android.software.input_methods":
            case "android.software.live_wallpaper":
            case "android.software.webview":
                return true;
            default:
                return false;
        }
    }

    static String resolveLabel(int labelRes) {
        if (labelRes == 0 || sApkPath == null) {
            return null;
        }
        try {
            if (sAppResources == null) {
                Class<?> assetManagerClass = Class.forName("android.content.res.AssetManager");
                Object assets = assetManagerClass.getDeclaredConstructor().newInstance();
                assetManagerClass.getMethod("addAssetPath", String.class)
                        .invoke(assets, sApkPath);
                Class<?> resourcesClass = Class.forName("android.content.res.Resources");
                Object system = resourcesClass.getMethod("getSystem").invoke(null);
                Object metrics = resourcesClass.getMethod("getDisplayMetrics").invoke(system);
                Object configuration = resourcesClass.getMethod("getConfiguration")
                        .invoke(system);
                sAppResources = resourcesClass.getConstructor(
                        assetManagerClass,
                        Class.forName("android.util.DisplayMetrics"),
                        Class.forName("android.content.res.Configuration"))
                        .newInstance(assets, metrics, configuration);
            }
            Object text = sAppResources.getClass().getMethod("getText", int.class)
                    .invoke(sAppResources, labelRes);
            return text != null ? text.toString() : null;
        } catch (Throwable t) {
            log("unable to resolve label 0x" + Integer.toHexString(labelRes) + ": " + t);
            return null;
        }
    }

    static Object findParsedActivity(Object pkg, String className) throws Exception {
        Object activity = findParsedActivityComponent(pkg, className);
        return activity == null ? null : getFieldValue(activity, "info");
    }

    static Object findParsedServiceComponent(Object pkg, String className)
            throws Exception {
        if (pkg == null || className == null) {
            return null;
        }
        List<?> services = (List<?>) getFieldValue(pkg, "services");
        for (Object service : services) {
            Object info = getFieldValue(service, "info");
            if (className.equals(getFieldValue(info, "name"))) {
                return service;
            }
        }
        return null;
    }

    static void installApplication() throws Exception {
        File sourceApk = new File(sApkPath);

        // PackageParser asks for the platform's split-permission table while
        // parsing an APK.  ActivityThread caches the service on first lookup,
        // so it must exist before PackageParser runs, not just before
        // ActivityThread.main().
        registerLocalService("permissionmgr",
                "android.permission.IPermissionManager",
                new PermissionManagerHandler());

        Object parsed = parsePackageWithPackageParser(sourceApk.getAbsolutePath());
        ManifestInfo info = manifestInfoFromPackage(parsed);
        if (info.packageName == null || info.packageName.isEmpty()) {
            throw new IllegalArgumentException(
                    "APK manifest does not declare a package: " + sourceApk);
        }
        if (sRequestedComponent == null && sServiceComponent == null
                && info.mainActivity == null) {
            throw new IllegalArgumentException(
                    "APK manifest has no MAIN/LAUNCHER activity: " + sourceApk);
        }
        sParsedPackage = parsed;

        resolveComponent(info);
        if (sServiceComponent != null) {
            Object service = findParsedServiceComponent(sParsedPackage, sClassName);
            if (service == null) {
                throw new IllegalArgumentException(
                        "service not found in manifest: " + sPackageName + "/" + sClassName);
            }
            sServiceInfo = generateComponentInfo("Service", service, 0);
            if (sServiceInfo == null) {
                throw new IllegalArgumentException(
                        "unable to generate ServiceInfo for " + sClassName);
            }
            String processName = (String) getFieldValue(sServiceInfo, "processName");
            sProcessName = processName == null || processName.isEmpty()
                    ? sPackageName : processName;
        }
        sApplicationClass = info.applicationClass == null || info.applicationClass.isEmpty()
                ? null
                : resolveClassName(info.packageName, info.applicationClass);
        sTargetSdk = info.targetSdk > 0 ? info.targetSdk : (info.minSdk > 0 ? info.minSdk : 1);
        sMinSdk = info.minSdk > 0 ? info.minSdk : 1;
        sLabel = info.label;
        sApplicationTheme = info.applicationTheme;
        sActivityTheme = info.mainActivityTheme != 0
                ? info.mainActivityTheme
                : info.applicationTheme;
        if (sLabel != null && (sLabel.startsWith("@") || sLabel.startsWith("?"))) {
            // Resource references cannot be resolved without a full package
            // parser; fall back to a null label.
            sLabel = null;
        }

        String androidData = System.getenv("ANDROID_DATA");
        if (androidData == null || androidData.isEmpty()) {
            androidData = "/tmp/droidlet-data";
        }

        File installDir = new File(androidData, "app/" + sPackageName);
        mkdirs(installDir);

        File installedApk = new File(installDir, "base.apk");
        if (!sourceApk.getCanonicalFile().equals(installedApk.getCanonicalFile())) {
            Files.copy(sourceApk.toPath(), installedApk.toPath(),
                    StandardCopyOption.REPLACE_EXISTING);
        }
        sApkPath = installedApk.getAbsolutePath();
        File nativeLibDir = new File(installDir, "lib/x86_64");
        if (sServiceComponent != null && nativeLibDir.isDirectory()) {
            // The parent already installed and extracted this app.  Do not
            // delete or re-extract libraries while the parent may have them
            // mapped; the child only needs the same paths.
            sNativeLibraryDir = nativeLibDir.getAbsolutePath();
        } else {
            sNativeLibraryDir = extractNativeLibraries(installDir, sourceApk);
        }

        if (sDataDir == null || sDataDir.isEmpty()) {
            sDataDir = new File(androidData, "data/" + sPackageName).getAbsolutePath();
        }

        if (sParsedPackage != null) {
            sParsedApplicationInfo = getFieldValue(sParsedPackage, "applicationInfo");
            sParsedActivityInfo = findParsedActivity(sParsedPackage, sClassName);
            if (sParsedActivityInfo == null) {
                sParsedActivityInfo = findLauncherActivity(sParsedPackage);
            }
            if (sParsedActivityInfo != null) {
                int theme = (Integer) getFieldValue(sParsedActivityInfo, "theme");
                if (theme != 0) {
                    sActivityTheme = theme;
                }
            } else if (sRequestedComponent != null) {
                throw new IllegalArgumentException(
                        "activity not found in manifest: " + sPackageName + "/" + sClassName);
            }
        }

        log("installed " + sPackageName + " (" + sClassName + ", targetSdk=" + sTargetSdk
                + ", minSdk=" + sMinSdk + ") from " + sourceApk + " to " + installDir);
    }

    static void installExternalStorage() {
        String androidData = System.getenv("ANDROID_DATA");
        if (androidData == null || androidData.isEmpty()) {
            androidData = "/tmp/droidlet-data";
        }
        File external = new File(androidData, "media/0");
        mkdirs(external);
        try {
            Class<?> envClass = Class.forName("android.os.Environment");
            Field currentUser = envClass.getDeclaredField("sCurrentUser");
            currentUser.setAccessible(true);
            Class<?> hostEnvClass = Class.forName("android.os.DroidletEnvironment");
            Object user = hostEnvClass.getConstructor(int.class, File.class)
                    .newInstance(0, external);
            currentUser.set(null, user);
            log("external storage: " + external.getAbsolutePath());
        } catch (Throwable t) {
            log("unable to install external storage: " + t);
        }
    }

    static void resolveComponent(ManifestInfo info) {
        if (sServiceComponent != null) {
            // A service host has no activity to launch; reuse the component
            // parsing to install the package and resolve the service class.
            String pkg = info.packageName;
            String cls = sServiceComponent;
            int slash = cls.indexOf('/');
            if (slash >= 0) {
                pkg = cls.substring(0, slash);
                cls = cls.substring(slash + 1);
            }
            sPackageName = pkg;
            sClassName = resolveClassName(pkg, cls);
            return;
        }
        if (sRequestedComponent != null) {
            String pkg = info.packageName;
            String cls = sRequestedComponent;
            int slash = cls.indexOf('/');
            if (slash >= 0) {
                pkg = cls.substring(0, slash);
                cls = cls.substring(slash + 1);
            }
            sPackageName = pkg;
            sClassName = resolveClassName(pkg, cls);
            return;
        }

        sPackageName = info.packageName;
        if (info.mainActivity == null) {
            throw new IllegalArgumentException("APK manifest has no MAIN/LAUNCHER activity: "
                    + sApkPath);
        }
        sClassName = info.mainActivity;
    }

    static String extractNativeLibraries(File installDir, File apk) {
        String abi = "x86_64";
        String prefix = "lib/" + abi + "/";
        File libDir = new File(installDir, "lib/" + abi);
        deleteRecursively(libDir);
        mkdirs(libDir);

        try (ZipFile zip = new ZipFile(apk)) {
            Enumeration<? extends ZipEntry> entries = zip.entries();
            while (entries.hasMoreElements()) {
                ZipEntry entry = entries.nextElement();
                String name = entry.getName();
                if (entry.isDirectory() || !name.startsWith(prefix) || !name.endsWith(".so")) {
                    continue;
                }
                File target = new File(libDir, name.substring(prefix.length()));
                try (InputStream in = zip.getInputStream(entry)) {
                    Files.copy(in, target.toPath(), StandardCopyOption.REPLACE_EXISTING);
                }
            }
        } catch (Throwable t) {
            log("unable to extract native libraries from " + apk + ": " + t);
        }
        return libDir.getAbsolutePath();
    }

}
