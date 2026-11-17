/*
 * Host-build replacement for android.net.TrafficStats.
 *
 * The real framework class loads libframework-connectivity-tiramisu-jni from
 * its static initializer.  That library belongs to the connectivity mainline
 * module and is not part of the droidlet host install.  RuntimeInit touches
 * TrafficStats even for applications that never use networking, so the eager
 * load aborts application startup.
 *
 * This overlay keeps the small API surface used by RuntimeInit, ActivityThread
 * and normal socket-tagging call sites, but does not load any JNI library.
 * Statistics are reported as unsupported and socket tagging is a no-op.
 */

package android.net;

import android.content.Context;

import java.io.FileDescriptor;
import java.net.DatagramSocket;
import java.net.Socket;
import java.net.SocketException;

public final class TrafficStats {
    public static final int UNSUPPORTED = -1;
    public static final int UID_REMOVED = -1;
    public static final int UID_TETHERING = -5;

    private static final int TAG_SYSTEM_APP = 0xFFFFFF00;
    private static final int TAG_SYSTEM_BACKUP = 0xFFFFFF01;
    private static final int TAG_SYSTEM_DOWNLOAD = 0xFFFFFF02;
    private static final int TAG_SYSTEM_MEDIA = 0xFFFFFF03;
    private static final int TAG_SYSTEM_RESTORE = 0xFFFFFF04;
    private static final int TAG_SYSTEM_PROBE = 0xFFFFFF05;

    private static final ThreadLocal<Integer> sThreadStatsTag = new ThreadLocal<>();
    private static final ThreadLocal<Integer> sThreadStatsUid = new ThreadLocal<>();

    private TrafficStats() {}

    /**
     * The real implementation installs the connectivity JNI-backed socket
     * tagger.  SocketTagger already has a no-op default, so do nothing here.
     */
    public static void attachSocketTagger() {}

    /**
     * The real implementation connects to the network stats service.
     */
    public static void init(Context context) {}

    public static int getAndSetThreadStatsTag(int tag) {
        Integer previous = sThreadStatsTag.get();
        sThreadStatsTag.set(tag);
        return previous == null ? 0 : previous;
    }

    public static int getThreadStatsTag() {
        Integer tag = sThreadStatsTag.get();
        return tag == null ? 0 : tag;
    }

    public static void setThreadStatsTag(int tag) {
        sThreadStatsTag.set(tag);
    }

    public static void clearThreadStatsTag() {
        sThreadStatsTag.remove();
    }

    public static void setThreadStatsTagApp() {
        setThreadStatsTag(TAG_SYSTEM_APP);
    }

    public static void setThreadStatsTagBackup() {
        setThreadStatsTag(TAG_SYSTEM_BACKUP);
    }

    public static void setThreadStatsTagDownload() {
        setThreadStatsTag(TAG_SYSTEM_DOWNLOAD);
    }

    public static void setThreadStatsTagRestore() {
        setThreadStatsTag(TAG_SYSTEM_RESTORE);
    }

    public static int getThreadStatsUid() {
        Integer uid = sThreadStatsUid.get();
        return uid == null ? 0 : uid;
    }

    public static void setThreadStatsUid(int uid) {
        sThreadStatsUid.set(uid);
    }

    public static void clearThreadStatsUid() {
        sThreadStatsUid.remove();
    }

    public static void setThreadStatsUidSelf() {
        // The real implementation uses Process.myUid().  The uid is only used
        // for statistics, which this overlay reports as unsupported.
    }

    public static void tagSocket(Socket socket) throws SocketException {}

    public static void untagSocket(Socket socket) throws SocketException {}

    public static void tagDatagramSocket(DatagramSocket socket) throws SocketException {}

    public static void untagDatagramSocket(DatagramSocket socket) throws SocketException {}

    public static void tagFileDescriptor(FileDescriptor fd) throws SocketException {}

    public static void untagFileDescriptor(FileDescriptor fd) throws SocketException {}

    public static void incrementOperationCount(int operationCount) {}

    public static void incrementOperationCount(int tag, int operationCount) {}

    public static long getUidRxBytes(int uid) {
        return UNSUPPORTED;
    }

    public static long getUidTxBytes(int uid) {
        return UNSUPPORTED;
    }

    public static long getUidRxPackets(int uid) {
        return UNSUPPORTED;
    }

    public static long getUidTxPackets(int uid) {
        return UNSUPPORTED;
    }

    public static long getUidTcpRxBytes(int uid) {
        return UNSUPPORTED;
    }

    public static long getUidTcpTxBytes(int uid) {
        return UNSUPPORTED;
    }

    public static long getUidTcpRxSegments(int uid) {
        return UNSUPPORTED;
    }

    public static long getUidTcpTxSegments(int uid) {
        return UNSUPPORTED;
    }

    public static long getUidUdpRxBytes(int uid) {
        return UNSUPPORTED;
    }

    public static long getUidUdpTxBytes(int uid) {
        return UNSUPPORTED;
    }

    public static long getUidUdpRxPackets(int uid) {
        return UNSUPPORTED;
    }

    public static long getUidUdpTxPackets(int uid) {
        return UNSUPPORTED;
    }

    public static long getUidStats(int uid, int type) {
        return UNSUPPORTED;
    }

    public static long getTotalRxBytes() {
        return UNSUPPORTED;
    }

    public static long getTotalTxBytes() {
        return UNSUPPORTED;
    }

    public static long getTotalRxPackets() {
        return UNSUPPORTED;
    }

    public static long getTotalTxPackets() {
        return UNSUPPORTED;
    }

    public static long getTotalStats(int type) {
        return UNSUPPORTED;
    }

    public static long getIfaceStats(String iface, int type) {
        return UNSUPPORTED;
    }

    public static long getRxBytes(String iface) {
        return UNSUPPORTED;
    }

    public static long getTxBytes(String iface) {
        return UNSUPPORTED;
    }

    public static long getRxPackets(String iface) {
        return UNSUPPORTED;
    }

    public static long getTxPackets(String iface) {
        return UNSUPPORTED;
    }

    public static long getMobileRxBytes() {
        return UNSUPPORTED;
    }

    public static long getMobileTxBytes() {
        return UNSUPPORTED;
    }

    public static long getMobileRxPackets() {
        return UNSUPPORTED;
    }

    public static long getMobileTxPackets() {
        return UNSUPPORTED;
    }

    public static long getMobileTcpRxPackets() {
        return UNSUPPORTED;
    }

    public static long getMobileTcpTxPackets() {
        return UNSUPPORTED;
    }

    public static long getLoopbackRxBytes() {
        return UNSUPPORTED;
    }

    public static long getLoopbackTxBytes() {
        return UNSUPPORTED;
    }

    public static long getLoopbackRxPackets() {
        return UNSUPPORTED;
    }

    public static long getLoopbackTxPackets() {
        return UNSUPPORTED;
    }

    public static String[] getMobileIfaces() {
        return new String[0];
    }

    /*
     * TrafficStats$SocketTagger in framework.jar refers to these private
     * methods.  Keep the same descriptors so the framework class stays
     * link-compatible, but implement them locally instead of through JNI.
     */
    private static int native_tagSocketFd(FileDescriptor fd, int tag, int uid) {
        return 0;
    }

    private static int native_untagSocketFd(FileDescriptor fd) {
        return 0;
    }
}
