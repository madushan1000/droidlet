package com.example.droidlet.exoplayerapi;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.widget.TextView;

import androidx.media3.common.MediaItem;
import androidx.media3.common.PlaybackException;
import androidx.media3.common.Player;
import androidx.media3.common.VideoSize;
import androidx.media3.exoplayer.ExoPlayer;

public class MainActivity extends Activity {
    private static final String TAG = "ExoPlayerApiTest";

    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private ExoPlayer mPlayer;
    private TextView mStatus;
    private boolean mSeeked;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        setContentView(R.layout.main);
        mStatus = findViewById(R.id.status);
        SurfaceView surfaceView = findViewById(R.id.surface);

        mPlayer = new ExoPlayer.Builder(this).build();
        mPlayer.addListener(new Player.Listener() {
            @Override
            public void onPlaybackStateChanged(int playbackState) {
                String name;
                switch (playbackState) {
                    case Player.STATE_IDLE:
                        name = "idle";
                        break;
                    case Player.STATE_BUFFERING:
                        name = "buffering";
                        break;
                    case Player.STATE_READY:
                        name = "ready";
                        break;
                    case Player.STATE_ENDED:
                        name = "ended";
                        break;
                    default:
                        name = Integer.toString(playbackState);
                }
                Log.i(TAG, "playback state: " + name);
                updateStatus("ExoPlayer state: " + name);
                if (playbackState == Player.STATE_READY && !mSeeked && mPlayer != null
                        && mPlayer.getCurrentMediaItemIndex() == 0) {
                    mSeeked = true;
                    Log.i(TAG, "seeking to 1000 ms");
                    mPlayer.seekTo(1000);
                }
            }

            @Override
            public void onPlayerError(PlaybackException error) {
                Log.e(TAG, "player error", error);
                updateStatus("ExoPlayer error: " + error.getErrorCodeName());
            }

            @Override
            public void onVideoSizeChanged(VideoSize videoSize) {
                Log.i(TAG, "video size: " + videoSize.width + "x" + videoSize.height
                        + " ratio=" + videoSize.pixelWidthHeightRatio);
            }

            @Override
            public void onRenderedFirstFrame() {
                Log.i(TAG, "rendered first frame");
            }

            @Override
            public void onMediaItemTransition(MediaItem mediaItem, int reason) {
                int index = mPlayer != null ? mPlayer.getCurrentMediaItemIndex() : -1;
                Log.i(TAG, "media item transition: index=" + index + " reason=" + reason);
            }
        });
        mPlayer.setVideoSurfaceView(surfaceView);
        surfaceView.getHolder().addCallback(new SurfaceHolder.Callback() {
            @Override
            public void surfaceCreated(SurfaceHolder holder) {
                if (mPlayer == null) {
                    return;
                }
                mPlayer.setVideoSurface(holder.getSurface());
                mPlayer.setMediaItems(java.util.Arrays.asList(
                        MediaItem.fromUri("asset:///sample.mp4"),
                        MediaItem.fromUri("asset:///sample_hevc.mp4")));
                mPlayer.prepare();
                mPlayer.setPlayWhenReady(true);
                Log.i(TAG, "ExoPlayer prepared");
            }

            @Override
            public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
            }

            @Override
            public void surfaceDestroyed(SurfaceHolder holder) {
                if (mPlayer != null) {
                    mPlayer.clearVideoSurface();
                }
            }
        });
    }

    private void updateStatus(final String text) {
        mHandler.post(new Runnable() {
            @Override
            public void run() {
                if (mStatus != null) {
                    mStatus.setText(text);
                }
            }
        });
    }

    @Override
    protected void onDestroy() {
        if (mPlayer != null) {
            mPlayer.release();
            mPlayer = null;
        }
        super.onDestroy();
    }
}
