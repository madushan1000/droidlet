/*
 * Window, display and input plumbing for DroidletLauncher.
 *
 * Droidlet runs the client-side window machinery (ViewRootImpl and its
 * SurfaceControl/BLASTBufferQueue) and stands in for WindowManagerService in
 * WindowSessionHandler: addToDisplay installs the app's InputChannel and
 * relayout fills the window frames.  DisplayManagerHandler supplies one
 * synthetic built-in display.  The dim layer behind dialogs is built here too.
 */

package android.app;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;

import static android.app.DroidletLauncher.*;
import static android.app.DroidletLauncherStartup.*;
import static android.app.DroidletLauncherActivities.*;

final class DroidletLauncherWindows {
    private DroidletLauncherWindows() {}

    static int windowChannelType(int channelId) {
        Integer type = sWindowChannelType.get(channelId);
        return type != null ? type : 0;
    }

    static void sortWindowInputChannels() {
        Collections.sort(sWindowInputChannels, new Comparator<Integer>() {
            @Override
            public int compare(Integer left, Integer right) {
                return Integer.compare(windowChannelType(left), windowChannelType(right));
            }
        });
    }

    static void activateTopInputChannel() {
        int top = sWindowInputChannels.isEmpty()
                ? 0 : sWindowInputChannels.get(sWindowInputChannels.size() - 1);
        sActiveInputChannel = top;
        if (top > 0) {
            nativeSetActiveInputChannel(top);
            Integer layerId = sWindowChannelLayerId.get(top);
            nativeSetActiveInputLayer(layerId != null ? layerId : 0);
            applyInputWindowOffset(top);
            nativeSetInputFocus(true);
            log("input: activated top window channel " + top
                    + " type=" + windowChannelType(top));
        } else {
            nativeSetInputFocus(false);
        }
    }

    static void applyInputWindowOffset(int channelId) {
        float insetX = 0.0f;
        float insetY = 0.0f;
        float[] insets = sWindowChannelInsets.get(channelId);
        if (insets != null && insets.length >= 2) {
            insetX = insets[0];
            insetY = insets[1];
        }
        nativeSetInputWindowInset(insetX, insetY);
        if (System.getenv("DROIDLET_LOG_WINDOW_ATTRS") != null) {
            log("input offset: channel=" + channelId + " insets=" + insetX + "," + insetY);
        }
        Integer layerId = sWindowChannelLayerId.get(channelId);
        if (layerId != null && layerId > 0) {
            try {
                float[] position = nativeHostLayerPosition(layerId);
                if (position != null && position.length >= 2) {
                    nativeSetInputWindowOffset(position[0] + insetX, position[1] + insetY);
                    return;
                }
            } catch (Throwable ignored) {
            }
        }
        float[] geometry = sWindowChannelGeometry.get(channelId);
        nativeSetInputWindowOffset((geometry != null ? geometry[0] : 0.0f) + insetX,
                (geometry != null ? geometry[1] : 0.0f) + insetY);
    }

    static final class WindowManagerHandler implements InvocationHandler {
        @Override
        public Object invoke(Object proxy, Method method, Object[] args) throws Throwable {
            String name = method.getName();
            if ("openSession".equals(name)) {
                return windowSession();
            }
            if ("hasNavigationBar".equals(name)) {
                return true;
            }
            if ("getCurrentAnimatorScale".equals(name)) {
                return 1.0f;
            }
            if ("getInitialDisplayId".equals(name)
                    || "getDefaultDisplayId".equals(name)) {
                return sDisplayId;
            }
            return defaultValue(method.getReturnType());
        }
    }

    static int installInputChannel(Object outInputChannel, Object windowAttrs) {
        if (outInputChannel == null) {
            return -1;
        }
        try {
            Class<?> channelClass = Class.forName("android.view.InputChannel");
            Object client = nativeCreateClientInputChannel();
            if (client == null) {
                log("input: native channel not available");
                return -1;
            }
            Method copyTo = channelClass.getMethod("copyTo", channelClass);
            copyTo.invoke(client, outInputChannel);
            final int id = nativeLastInputChannelId();
            Object token = null;
            int type = 0;
            if (windowAttrs != null) {
                try {
                    token = getFieldValue(windowAttrs, "token");
                } catch (Throwable ignored) {
                    // Attr token is absent; the channel still works.
                }
                try {
                    type = (Integer) getFieldValue(windowAttrs, "type");
                } catch (Throwable ignored) {
                    // Older windows may not expose type.
                }
            }
            // Popup/dialog windows often inherit the activity token.  Only
            // remember the channel on the activity record for the activity's
            // own base window; sub-windows are tracked separately so they do
            // not replace the activity's input channel.
            if (token != null && type == 1) {
                ActivityRecord record = findRecordByToken(token);
                if (record != null) {
                    record.inputChannelId = id;
                }
            }
            log("input: installed window InputChannel id=" + id + " type=" + type);
            return id;
        } catch (Throwable t) {
            log("input: unable to install channel: " + t);
            t.printStackTrace(System.err);
            return -1;
        }
    }

    static void notifyWindowFocus() {
        try {
            nativeSetInputFocus(true);
            log("input: queued focus and touch mode");
        } catch (Throwable t) {
            log("input: unable to queue focus: " + t);
        }
    }

    static synchronized Object windowSession() throws Exception {
        if (sWindowSession == null) {
            Class<?> iface = Class.forName("android.view.IWindowSession");
            sWindowSession = Proxy.newProxyInstance(
                    iface.getClassLoader(),
                    new Class<?>[] {iface},
                    new WindowSessionHandler());
        }
        return sWindowSession;
    }

    static final class WindowSessionHandler implements InvocationHandler {
        private boolean mFirstRelayout = true;

        @Override
        public Object invoke(Object proxy, Method method, Object[] args) throws Throwable {
            String name = method.getName();
            if ("addToDisplay".equals(name) || "addToDisplayAsUser".equals(name)) {
                final int channelIndex = "addToDisplayAsUser".equals(name) ? 6 : 5;
                final Object window = args.length > 0 ? args[0] : null;
                final Object attrs = args.length > 1 ? args[1] : null;
                if (window != null && attrs != null) {
                    sWindowAttrsByWindow.put(window, attrs);
                }
                int channelId = -1;
                if (args.length > channelIndex) {
                    channelId = installInputChannel(args[channelIndex], attrs);
                }
                if (channelId > 0) {
                    Integer previous = sWindowChannelByWindow.put(window, channelId);
                    if (previous != null) {
                        sWindowInputChannels.remove(previous);
                    }
                    boolean touchable = true;
                    boolean tooltip = false;
                    int type = 0;
                    int flags = 0;
                    try {
                        flags = attrs != null ? (Integer) getFieldValue(attrs, "flags") : 0;
                        type = attrs != null ? (Integer) getFieldValue(attrs, "type") : 0;
                        Class<?> layoutParams =
                                Class.forName("android.view.WindowManager$LayoutParams");
                        int notTouchable = staticInt(layoutParams, "FLAG_NOT_TOUCHABLE", 0x10);
                        int notFocusable = staticInt(layoutParams, "FLAG_NOT_FOCUSABLE", 0x8);
                        int noLimits = staticInt(layoutParams, "FLAG_LAYOUT_NO_LIMITS", 0x200);
                        // Compose tooltips are Popups with clipping disabled: they are
                        // non-focusable and set FLAG_LAYOUT_NO_LIMITS (menus keep clipping
                        // enabled).  They must never become the active input window, or a
                        // single 90x24 tooltip swallows every touch on the screen.
                        tooltip = (flags & notFocusable) != 0 && (flags & noLimits) != 0;
                        touchable = (flags & notTouchable) == 0 && !tooltip;
                    } catch (Throwable ignored) {
                    }
                    if (System.getenv("DROIDLET_LOG_WINDOW_ATTRS") != null) {
                        log("input: window add channel=" + channelId + " type=" + type
                                + " flags=0x" + Integer.toHexString(flags)
                                + " touchable=" + touchable + " tooltip=" + tooltip
                                + " title=" + getFieldValue(attrs, "mTitle")
                                + " pkg=" + getFieldValue(attrs, "packageName"));
                    }
                    sWindowChannelType.put(channelId, type);
                    if (touchable) {
                        sWindowInputChannels.add(channelId);
                        sortWindowInputChannels();
                        activateTopInputChannel();
                    }
                }
                // Focus is an input message and is buffered until
                // ViewRootImpl installs its InputEventReceiver.
                notifyWindowFocus();
                // WindowManagerGlobal.ADD_OKAY | WindowManagerGlobal.ADD_FLAG_APP_VISIBLE.
                // ViewRootImpl::getHostVisibility() reports GONE unless the latter is set,
                // which would skip SurfaceControl creation and all drawing.
                return 0x2;
            }
            if ("relayout".equals(name) || "relayout2".equals(name)) {
                // With WindowManager.useClientSurface() ViewRootImpl already owns the
                // SurfaceControl and the BLASTBufferQueue; the session only has to
                // describe the window geometry and acknowledge the relayout.  A real
                // WindowManagerService fills these frames in from its WindowState.
                final int requestedWidth = args[2] != null ? (Integer) args[2] : 0;
                final int requestedHeight = args[3] != null ? (Integer) args[3] : 0;
                final int seq = args[6] != null ? (Integer) args[6] : 0;
                // relayout() has outRelayoutResult at 8 and outSurface at 9;
                // relayout2() has the SurfaceControl at 8 and the result at 9.
                final int resultIndex = "relayout2".equals(name) ? 9 : 8;
                Object result = args.length > resultIndex ? args[resultIndex] : null;
                final Object relayoutWindowArg = args.length > 0 ? args[0] : null;
                Object attrs = args.length > 1 ? args[1] : null;
                if (attrs != null) {
                    if (relayoutWindowArg != null) {
                        sWindowAttrsByWindow.put(relayoutWindowArg, attrs);
                    }
                } else if (relayoutWindowArg != null) {
                    attrs = sWindowAttrsByWindow.get(relayoutWindowArg);
                }
                if (System.getenv("DROIDLET_LOG_WINDOW_ATTRS") != null) {
                    log("relayout request " + name + " seq=" + seq + " w=" + requestedWidth
                            + " h=" + requestedHeight + " attrs=" + (attrs != null));
                }
                int[] geometry = fillWindowFrames(result, seq, requestedWidth, requestedHeight,
                        attrs);
                if ("relayout2".equals(name) && args.length > 8 && args[8] != null) {
                    applyWindowSurface(args[8], attrs, geometry);
                }
                Object relayoutWindow = args.length > 0 ? args[0] : null;
                Integer relayoutChannel = sWindowChannelByWindow.get(relayoutWindow);
                if ("relayout2".equals(name) && args.length > 8 && args[8] != null) {
                    try {
                        int layerId = (Integer) args[8].getClass().getMethod("getLayerId")
                                .invoke(args[8]);
                        sWindowLayerId.put(relayoutWindow, layerId);
                        if (relayoutChannel != null) {
                            sWindowChannelLayerId.put(relayoutChannel, layerId);
                        }
                    } catch (Throwable ignored) {
                    }
                }
                if (relayoutChannel != null) {
                    sWindowChannelGeometry.put(relayoutChannel,
                            new float[] {geometry[0], geometry[1]});
                    if (geometry.length >= 6) {
                        sWindowChannelInsets.put(relayoutChannel,
                                new float[] {geometry[4], geometry[5]});
                    }
                    if (relayoutChannel == sActiveInputChannel) {
                        Integer layerId = sWindowChannelLayerId.get(relayoutChannel);
                        nativeSetActiveInputLayer(layerId != null ? layerId : 0);
                        applyInputWindowOffset(relayoutChannel);
                    }
                }
                updateDimLayer(attrs);
                // WindowManagerGlobal.RELAYOUT_RES_FIRST_TIME on the first relayout
                // only, so ViewRootImpl reports the first draw once.
                if (mFirstRelayout) {
                    mFirstRelayout = false;
                    return 1;
                }
                return 0;
            }
            if ("remove".equals(name)) {
                Object window = args.length > 0 ? args[0] : null;
                sWindowAttrsByWindow.remove(window);
                Integer layerId = sWindowLayerId.remove(window);
                if (layerId != null && layerId > 0) {
                    nativeHostRemoveLayer(layerId);
                }
                Integer removedChannel = sWindowChannelByWindow.remove(window);
                if (removedChannel != null) {
                    sWindowInputChannels.remove(removedChannel);
                    sWindowChannelGeometry.remove(removedChannel);
                    sWindowChannelInsets.remove(removedChannel);
                    sWindowChannelType.remove(removedChannel);
                    sWindowChannelLayerId.remove(removedChannel);
                    if (sActiveInputChannel == removedChannel) {
                        activateTopInputChannel();
                    }
                }
                if (sDimVisible && sDimLayer != null) {
                    applyDimTransaction(sDimLayer, false, 0.0f);
                    sDimVisible = false;
                }
                return null;
            }
            if ("getDisplayId".equals(name)) {
                return sDisplayId;
            }
            if ("outOfMemory".equals(name)) {
                return false;
            }
            return defaultValue(method.getReturnType());
        }
    }

    static int[] fillWindowFrames(Object result, int seq, int requestedWidth,
            int requestedHeight, Object attrs) {
        final int displayWidth = displayWidth();
        final int displayHeight = displayHeight();
        int width = requestedWidth > 0 ? requestedWidth : displayWidth;
        int height = requestedHeight > 0 ? requestedHeight : displayHeight;
        int left = 0;
        int top = 0;
        int insetLeft = 0;
        int insetTop = 0;
        if (attrs != null) {
            try {
                int layoutWidth = (Integer) getFieldValue(attrs, "width");
                int layoutHeight = (Integer) getFieldValue(attrs, "height");
                if (layoutWidth == -1) {
                    width = displayWidth;
                } else if (layoutWidth > 0) {
                    width = layoutWidth;
                }
                if (layoutHeight == -1) {
                    height = displayHeight;
                } else if (layoutHeight > 0) {
                    height = layoutHeight;
                }
                int gravity = (Integer) getFieldValue(attrs, "gravity");
                int x = (Integer) getFieldValue(attrs, "x");
                int y = (Integer) getFieldValue(attrs, "y");
                Object surfaceInsets = getFieldValue(attrs, "surfaceInsets");
                if (surfaceInsets != null) {
                    insetLeft = (Integer) getFieldValue(surfaceInsets, "left");
                    insetTop = (Integer) getFieldValue(surfaceInsets, "top");
                }
                if (System.getenv("DROIDLET_LOG_WINDOW_ATTRS") != null) {
                    Object insets = getFieldValue(attrs, "surfaceInsets");
                    log("window attrs: type=" + getFieldValue(attrs, "type")
                            + " x=" + x + " y=" + y + " w=" + layoutWidth
                            + " h=" + layoutHeight + " gravity=0x"
                            + Integer.toHexString(gravity) + " surfaceInsets=" + insets
                            + " insetLeft=" + insetLeft + " insetTop=" + insetTop);
                }
                Class<?> gravityClass = Class.forName("android.view.Gravity");
                // Compare the gravity axis, not individual bits: LEFT (0x3)
                // and TOP (0x30) both contain the CENTER_* bits, so a naive
                // bit test would center every TOP|LEFT popup and tooltip.
                int horizontalMask = staticInt(gravityClass,
                        "HORIZONTAL_GRAVITY_MASK", 0x00000007);
                int verticalMask = staticInt(gravityClass,
                        "VERTICAL_GRAVITY_MASK", 0x00000070);
                int centerHorizontal = staticInt(gravityClass,
                        "CENTER_HORIZONTAL", 0x00000001);
                int centerVertical = staticInt(gravityClass,
                        "CENTER_VERTICAL", 0x00000010);
                int right = staticInt(gravityClass, "RIGHT", 0x00000005);
                int bottom = staticInt(gravityClass, "BOTTOM", 0x00000050);
                int horizontalGravity = gravity & horizontalMask;
                int verticalGravity = gravity & verticalMask;
                // x and y are offsets from the gravity-derived position, not
                // absolute coordinates: a centred tooltip passes its anchor
                // offset in x and expects the window to be centred and then
                // shifted (TooltipPopup computes x relative to the app centre).
                if (horizontalGravity == centerHorizontal) {
                    left = (displayWidth - width) / 2 + x;
                } else if (horizontalGravity == right) {
                    left = displayWidth - width - x;
                } else {
                    left = x;
                }
                if (verticalGravity == centerVertical) {
                    top = (displayHeight - height) / 2 + y;
                } else if (verticalGravity == bottom) {
                    top = displayHeight - height - y;
                } else {
                    top = y;
                }
                // Gravity.applyDisplay(): keep app windows and sub-windows on
                // screen (TooltipPopup near the right edge would otherwise be
                // clipped).
                if (top < 0) {
                    top = 0;
                } else if (top + height > displayHeight) {
                    top = Math.max(0, displayHeight - height);
                }
                if (left < 0) {
                    left = 0;
                } else if (left + width > displayWidth) {
                    left = Math.max(0, displayWidth - width);
                }
                if (System.getenv("DROIDLET_LOG_WINDOW_ATTRS") != null) {
                    log("window frame: " + left + "," + top + " " + width + "x" + height);
                }
            } catch (Throwable t) {
                log("unable to read window attributes: " + t);
            }
        }
        if (result == null) {
            return new int[] {left, top, width, height, insetLeft, insetTop};
        }
        try {
            Object frames = getFieldValue(result, "frames");
            if (frames == null) {
                return new int[] {left, top, width, height, insetLeft, insetTop};
            }
            Class<?> rectClass = Class.forName("android.graphics.Rect");
            Method set = rectClass.getMethod("set", int.class, int.class, int.class, int.class);
            set.invoke(getFieldValue(frames, "frame"), left, top, left + width, top + height);
            set.invoke(getFieldValue(frames, "displayFrame"), 0, 0, displayWidth, displayHeight);
            set.invoke(getFieldValue(frames, "parentFrame"), 0, 0, displayWidth, displayHeight);
            setField(frames, "compatScale", 1.0f);
            setField(frames, "seq", seq);
        } catch (Throwable t) {
            log("unable to fill window frames: " + t);
        }
        return new int[] {left, top, width, height, insetLeft, insetTop};
    }

    static void updateDimLayer(Object attrs) {
        try {
            int flags = attrs != null ? (Integer) getFieldValue(attrs, "flags") : 0;
            float dimAmount = attrs != null ? (Float) getFieldValue(attrs, "dimAmount") : 0.0f;
            int dimFlag = staticInt(
                    Class.forName("android.view.WindowManager$LayoutParams"),
                    "FLAG_DIM_BEHIND", 0x00000002);
            boolean want = (flags & dimFlag) != 0 && dimAmount > 0.0f;
            if (!want) {
                // A non-dimming window (such as the activity underneath the
                // dialog) must not clear the dim layer requested by another
                // window.  remove() clears it when the dimming window goes away.
                return;
            }
            if (sDimLayer == null) {
                sDimLayer = createDimLayer();
                if (sDimLayer == null) {
                    return;
                }
            }
            applyDimTransaction(sDimLayer, true, dimAmount);
            sDimVisible = true;
        } catch (Throwable t) {
            log("unable to update dim layer: " + t);
        }
    }

    static Object createDimLayer() {
        try {
            Class<?> sessionClass = Class.forName("android.view.SurfaceSession");
            Object session = sessionClass.getDeclaredConstructor().newInstance();
            Class<?> builderClass = Class.forName("android.view.SurfaceControl$Builder");
            Object builder = builderClass.getConstructor(sessionClass).newInstance(session);
            builderClass.getMethod("setName", String.class).invoke(builder, "droidlet-dim");
            builderClass.getMethod("setColorLayer").invoke(builder);
            return builderClass.getMethod("build").invoke(builder);
        } catch (Throwable t) {
            log("unable to create dim layer: " + t);
            return null;
        }
    }

    static void applyDimTransaction(Object surfaceControl, boolean visible, float alpha) {
        try {
            Class<?> surfaceControlClass = Class.forName("android.view.SurfaceControl");
            Class<?> transactionClass =
                    Class.forName("android.view.SurfaceControl$Transaction");
            Object transaction = transactionClass.getDeclaredConstructor().newInstance();
            transactionClass.getMethod("setVisibility", surfaceControlClass, boolean.class)
                    .invoke(transaction, surfaceControl, visible);
            if (visible) {
                transactionClass.getMethod("setColor", surfaceControlClass, float[].class)
                        .invoke(transaction, surfaceControl, new float[] {0.0f, 0.0f, 0.0f});
                transactionClass.getMethod("setAlpha", surfaceControlClass, float.class)
                        .invoke(transaction, surfaceControl, alpha);
                transactionClass.getMethod("setLayer", surfaceControlClass, int.class)
                        .invoke(transaction, surfaceControl, 5);
                transactionClass.getMethod("setPosition", surfaceControlClass,
                                float.class, float.class)
                        .invoke(transaction, surfaceControl, 0.0f, 0.0f);
                transactionClass.getMethod("setDestinationFrame", surfaceControlClass,
                                int.class, int.class)
                        .invoke(transaction, surfaceControl,
                                displayWidth(), displayHeight());
            }
            transactionClass.getMethod("apply").invoke(transaction);
        } catch (Throwable t) {
            log("unable to apply dim layer: " + t);
        }
    }

    static void applyWindowSurface(Object surfaceControl, Object attrs, int[] geometry) {
        if (surfaceControl == null || geometry == null || geometry.length < 4) {
            return;
        }
        try {
            float alpha = 1.0f;
            int layer = 0;
            if (attrs != null) {
                alpha = (Float) getFieldValue(attrs, "alpha");
                int type = (Integer) getFieldValue(attrs, "type");
                int application = staticInt(
                        Class.forName("android.view.WindowManager$LayoutParams"),
                        "TYPE_APPLICATION", 2);
                int applicationOverlay = staticInt(
                        Class.forName("android.view.WindowManager$LayoutParams"),
                        "TYPE_APPLICATION_OVERLAY", 2038);
                if (type == 1 /* TYPE_BASE_APPLICATION */) {
                    layer = 0;
                } else if (type == application) {
                    layer = 10;
                } else if (type >= 1000 && type < 2000) {
                    // Panels, sub-panels and attached dialogs (popup windows).
                    layer = 20;
                } else if (type >= applicationOverlay) {
                    layer = 30;
                } else {
                    layer = 10;
                }
            }
            Class<?> surfaceControlClass = Class.forName("android.view.SurfaceControl");
            Class<?> transactionClass =
                    Class.forName("android.view.SurfaceControl$Transaction");
            // The frame is the content rect; the surface is expanded by surfaceInsets, so
            // its origin is the frame minus the insets (WindowState's mSurfacePosition).
            // Without this, every popup (menu, tooltip, dialog) is drawn inset.left/top
            // pixels down-right of where the app expects it.
            int insetLeft = geometry.length >= 5 ? geometry[4] : 0;
            int insetTop = geometry.length >= 6 ? geometry[5] : 0;
            Object transaction = transactionClass.getDeclaredConstructor().newInstance();
            transactionClass.getMethod("setPosition", surfaceControlClass,
                            float.class, float.class)
                    .invoke(transaction, surfaceControl,
                            (float) (geometry[0] - insetLeft),
                            (float) (geometry[1] - insetTop));
            transactionClass.getMethod("setAlpha", surfaceControlClass, float.class)
                    .invoke(transaction, surfaceControl, alpha);
            transactionClass.getMethod("setLayer", surfaceControlClass, int.class)
                    .invoke(transaction, surfaceControl, layer);
            transactionClass.getMethod("apply").invoke(transaction);
        } catch (Throwable t) {
            log("unable to apply window surface transaction: " + t);
        }
    }

    static final class DisplayManagerHandler implements InvocationHandler {
        @Override
        public Object invoke(Object proxy, Method method, Object[] args) throws Throwable {
            String name = method.getName();
            if ("getDisplayInfo".equals(name)) {
                int displayId = (Integer) args[0];
                return displayId == sDisplayId ? defaultDisplayInfo() : null;
            }
            if ("getDisplayIds".equals(name)) {
                return new int[] {sDisplayId};
            }
            if ("getPreferredWideGamutColorSpaceId".equals(name)) {
                return 0;
            }
            if ("getOverlaySupport".equals(name)) {
                return Class.forName("android.hardware.OverlayProperties")
                        .getMethod("getDefault").invoke(null);
            }
            return defaultValue(method.getReturnType());
        }
    }

    /*
     * Update the cached synthetic DisplayInfo in place.  ResourcesManager and
     * Display hold references to this object, so replacing it would leave them
     * with the old size.
     */
    static synchronized void updateDisplaySize(int width, int height) {
        if (sDefaultDisplayInfo == null) {
            return;
        }
        try {
            Object info = sDefaultDisplayInfo;
            int densityDpi = displayDensityDpi();
            setField(info, "appWidth", width);
            setField(info, "appHeight", height);
            setField(info, "smallestNominalAppWidth", width);
            setField(info, "smallestNominalAppHeight", height);
            setField(info, "largestNominalAppWidth", width);
            setField(info, "largestNominalAppHeight", height);
            setField(info, "logicalWidth", width);
            setField(info, "logicalHeight", height);
            setField(info, "logicalDensityDpi", densityDpi);
            setField(info, "physicalXDpi", (float) densityDpi);
            setField(info, "physicalYDpi", (float) densityDpi);
            Class<?> modeClass = Class.forName("android.view.Display$Mode");
            Object mode = modeClass.getConstructor(int.class, int.class, int.class, float.class)
                    .newInstance(0, width, height, 60.0f);
            Object modes = java.lang.reflect.Array.newInstance(modeClass, 1);
            java.lang.reflect.Array.set(modes, 0, mode);
            setField(info, "supportedModes", modes);
        } catch (Throwable t) {
            log("unable to update display size: " + t);
        }
    }

    static synchronized Object defaultDisplayInfo() throws Exception {
        if (sDefaultDisplayInfo != null) {
            return sDefaultDisplayInfo;
        }
        final int width = displayWidth();
        final int height = displayHeight();
        final int densityDpi = displayDensityDpi();
        final float refreshRate = 60.0f;

        Class<?> modeClass = Class.forName("android.view.Display$Mode");
        Object mode = modeClass.getConstructor(int.class, int.class, int.class, float.class)
                .newInstance(0, width, height, refreshRate);
        Object modes = java.lang.reflect.Array.newInstance(modeClass, 1);
        java.lang.reflect.Array.set(modes, 0, mode);

        Class<?> infoClass = Class.forName("android.view.DisplayInfo");
        Object info = infoClass.getDeclaredConstructor().newInstance();
        setField(info, "displayId", sDisplayId);
        setField(info, "layerStack", 0);
        setField(info, "displayGroupId", 0);
        setField(info, "type", 1 /* Display.TYPE_INTERNAL */);
        setField(info, "flags", 0);
        setField(info, "name", "droidlet display");
        setField(info, "uniqueId", "local:" + sDisplayId);
        setField(info, "appWidth", width);
        setField(info, "appHeight", height);
        setField(info, "smallestNominalAppWidth", width);
        setField(info, "smallestNominalAppHeight", height);
        setField(info, "largestNominalAppWidth", width);
        setField(info, "largestNominalAppHeight", height);
        setField(info, "logicalWidth", width);
        setField(info, "logicalHeight", height);
        setField(info, "rotation", 0);
        setField(info, "modeId", 0);
        setField(info, "defaultModeId", 0);
        setField(info, "renderFrameRate", refreshRate);
        setField(info, "supportedRefreshRates", new float[] {refreshRate});
        setField(info, "supportedModes", modes);
        // Display.getFrameRateVelocityMapping() calls isEmpty() on this list
        // without a null check; View.convertVelocityToFrameRate() reads it
        // once a scroll/fling starts voting for a preferred frame rate.
        setField(info, "frameRateVelocityMapping", new ArrayList<Object>());
        setField(info, "logicalDensityDpi", densityDpi);
        setField(info, "physicalXDpi", (float) densityDpi);
        setField(info, "physicalYDpi", (float) densityDpi);
        setField(info, "state", 2 /* Display.STATE_ON */);
        setField(info, "committedState", 2);
        setField(info, "ownerUid", 0);
        setField(info, "ownerPackageName", null);
        sDefaultDisplayInfo = info;
        return info;
    }

}
