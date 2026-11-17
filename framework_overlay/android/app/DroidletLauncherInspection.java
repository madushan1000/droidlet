/*
 * Screenshot capture and window-state diagnostics for DroidletLauncher.
 *
 * The launcher copies the last composed frame out of the presenter, writes it
 * as a PNG through the framework Bitmap, and logs the visible TextViews.  This
 * is test/diagnostic code and is kept out of the main launcher state machine.
 */

package android.app;

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStream;

import static android.app.DroidletLauncher.*;
import static android.app.DroidletLauncherStartup.*;

final class DroidletLauncherInspection {
    private DroidletLauncherInspection() {}

    static void scheduleScreenshot(final Object thread, final int attempt) {
        try {
            Object handler = Class.forName("android.app.ActivityThread")
                    .getMethod("getHandler").invoke(thread);
            Runnable capture = new Runnable() {
                @Override
                public void run() {
                    boolean done;
                    try {
                        done = captureScreenshot(thread);
                    } catch (Throwable t) {
                        log("unable to capture window: " + t);
                        t.printStackTrace(System.err);
                        done = false;
                    }
                    if (!done && attempt < 20) {
                        scheduleScreenshot(thread, attempt + 1);
                        return;
                    }
                    if (System.getenv("DROIDLET_EXIT_AFTER_RESUME") != null) {
                        long holdMs = envLong("DROIDLET_PRESENT_HOLD_MS", 0);
                        if (composerActive() && holdMs > 0) {
                            // Keep the compositor connection (and the window)
                            // alive long enough for a screenshot/test harness.
                            final boolean success = done;
                            Runnable exit = new Runnable() {
                                @Override
                                public void run() {
                                    System.out.flush();
                                    System.err.flush();
                                    System.exit(success ? 0 : 1);
                                }
                            };
                            try {
                                Class.forName("android.os.Handler")
                                        .getMethod("postDelayed", Runnable.class, long.class)
                                        .invoke(handler, exit, holdMs);
                                return;
                            } catch (Throwable t) {
                                log("unable to delay exit: " + t);
                            }
                        }
                        System.out.flush();
                        System.err.flush();
                        System.exit(done ? 0 : 1);
                    }
                }
            };
            final long initialDelay = Math.max(100L,
                    envLong("DROIDLET_SCREENSHOT_DELAY_MS", 0));
            Class.forName("android.os.Handler")
                    .getMethod("postDelayed", Runnable.class, long.class)
                    .invoke(handler, capture, attempt == 0 ? initialDelay : 100L);
        } catch (Throwable t) {
            log("unable to schedule screenshot: " + t);
        }
    }

    static String screenshotPath() {
        if (sScreenshotPath != null) {
            return sScreenshotPath.isEmpty() ? null : sScreenshotPath;
        }
        String data = System.getenv("ANDROID_DATA");
        File base = (data != null && !data.isEmpty()) ? new File(data) : new File(".");
        return new File(base, "droidlet-screenshot.png").getAbsolutePath();
    }

    static boolean captureScreenshot(Object thread) throws Exception {
        String path = screenshotPath();
        if (path == null) {
            log("screenshot disabled");
            return true;
        }
        if (!composerActive()) {
            log("screenshot: host composer is not running");
            return false;
        }
        if (!loadPresenterLibrary()) {
            log("screenshot: native library unavailable");
            return false;
        }

        if (!nativeHostWaitFrame(500)) {
            log("screenshot: no composed frame yet");
            return false;
        }
        final int width = nativeHostFrameWidth();
        final int height = nativeHostFrameHeight();
        if (width <= 0 || height <= 0) {
            log("screenshot: composer has no frame size");
            return false;
        }

        int[] pixels = new int[width * height];
        int copied = nativeHostCopyFrame(pixels);
        if (copied != pixels.length) {
            log("screenshot: copied " + copied + " of " + pixels.length + " pixels");
            return false;
        }

        Class<?> bitmapClass = Class.forName("android.graphics.Bitmap");
        Class<?> configClass = Class.forName("android.graphics.Bitmap$Config");
        Object config = enumConstant(configClass, "ARGB_8888");
        Object bitmap = bitmapClass
                .getMethod("createBitmap", int[].class, int.class, int.class, configClass)
                .invoke(null, pixels, width, height, config);

        int opaque = 0;
        int ink = 0;
        int reference = pixels[0];
        long hash = 0xcbf29ce484222325L;
        for (int i = 0; i < pixels.length; i++) {
            int p = pixels[i];
            if ((p >>> 24) != 0) {
                opaque++;
            }
            if (p != reference) {
                ink++;
            }
            if ((i & 0x3f) == 0) {
                hash ^= p;
                hash *= 0x100000001b3L;
            }
        }

        File file = new File(path);
        File parent = file.getParentFile();
        if (parent != null) {
            mkdirs(parent);
        }
        Class<?> formatClass = Class.forName("android.graphics.Bitmap$CompressFormat");
        Object png = enumConstant(formatClass, "PNG");
        boolean written;
        try (OutputStream out = new FileOutputStream(file)) {
            Object result = bitmapClass
                    .getMethod("compress", formatClass, int.class, OutputStream.class)
                    .invoke(bitmap, png, 100, out);
            written = Boolean.TRUE.equals(result);
        } finally {
            bitmapClass.getMethod("recycle").invoke(bitmap);
        }

        log("screenshot: " + width + "x" + height
                + " opaque=" + opaque + "/" + pixels.length
                + " ink=" + ink
                + " hash=" + Long.toHexString(hash)
                + " -> " + file.getAbsolutePath()
                + " (" + file.length() + " bytes, written=" + written + ")");
        Object record = findActivityRecord(thread);
        Object activity = record != null ? getFieldValue(record, "activity") : null;
        Object decor = activity != null ? getFieldValue(activity, "mDecor") : null;
        if (decor != null) {
            log("screenshot: text views: " + describeTextViews(decor));
        }
        try {
            long[] stats = nativeHostFrameStats();
            if (stats != null && stats.length >= 3) {
                log("frame stats: latched=" + stats[0] + " presented=" + stats[1]
                        + " dropped=" + stats[2]);
            }
        } catch (Throwable t) {
            log("unable to read frame stats: " + t);
        }
        return written && file.length() > 0;
    }

    static String describeTextViews(Object view) {
        StringBuilder text = new StringBuilder();
        collectTextViews(view, text, 0);
        return text.length() == 0 ? "(none)" : text.toString();
    }

    static void collectTextViews(Object view, StringBuilder out, int depth) {
        if (view == null || depth > 64 || out.length() > 512) {
            return;
        }
        try {
            Class<?> viewClass = Class.forName("android.view.View");
            Class<?> textViewClass = Class.forName("android.widget.TextView");
            if (textViewClass.isInstance(view)) {
                Object sequence = textViewClass.getMethod("getText").invoke(view);
                if (sequence != null) {
                    if (out.length() > 0) {
                        out.append(" | ");
                    }
                    out.append('"').append(sequence).append('"');
                }
            }
            Class<?> groupClass = Class.forName("android.view.ViewGroup");
            if (groupClass.isInstance(view)) {
                int count = (Integer) groupClass.getMethod("getChildCount").invoke(view);
                for (int i = 0; i < count; i++) {
                    Object child = groupClass.getMethod("getChildAt", int.class)
                            .invoke(view, i);
                    collectTextViews(child, out, depth + 1);
                }
            }
        } catch (Throwable t) {
            // Diagnostics only: never fail the capture because of the walk.
        }
    }

    static void logWindowState(Object thread) throws Exception {
        Object record = findActivityRecord(thread);
        if (record == null) {
            log("window state: no activity record");
            return;
        }
        Object activity = getFieldValue(record, "activity");
        Object window = getFieldValue(record, "window");
        Object decor = getFieldValue(activity, "mDecor");
        boolean added = (Boolean) getFieldValue(activity, "mWindowAdded");
        int roots = -1;
        try {
            Class<?> globalClass = Class.forName("android.view.WindowManagerGlobal");
            Object global = globalClass.getMethod("getInstance").invoke(null);
            String[] names = (String[]) globalClass.getMethod("getViewRootNames")
                    .invoke(global);
            roots = names.length;
        } catch (Throwable t) {
            log("unable to count view roots: " + t);
        }
        log("window state: window=" + (window != null)
                + ", decor=" + (decor != null)
                + ", mWindowAdded=" + added
                + ", viewRoots=" + roots);
    }

    static int lifecycleState(Object thread) {
        try {
            Object record = findActivityRecord(thread);
            if (record == null) {
                return -2;
            }
            return (Integer) record.getClass().getMethod("getLifecycleState").invoke(record);
        } catch (Throwable t) {
            log("unable to read lifecycle state: " + t);
            return -3;
        }
    }

    static String stateName(int state) {
        switch (state) {
            case 0: return "PRE_ON_CREATE";
            case 1: return "ON_CREATE";
            case 2: return "ON_START";
            case 3: return "ON_RESUME";
            case 4: return "ON_PAUSE";
            case 5: return "ON_STOP";
            case 6: return "ON_DESTROY";
            case 7: return "ON_RESTART";
            default: return "UNKNOWN(" + state + ")";
        }
    }

}
