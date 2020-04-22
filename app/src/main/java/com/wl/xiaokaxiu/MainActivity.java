package com.wl.xiaokaxiu;

import androidx.annotation.Nullable;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import android.Manifest;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.util.Log;
import android.view.View;
import android.widget.Button;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;

public class MainActivity extends BaseActivity {

    private static final String TAG = "XiaoKaXiu";

    // Used to load the 'native-lib' library on application startup.
    static {
        System.loadLibrary("native-lib");
    }

    private static final int AUDIO_FILE_SELECT_CODE = 100;
    private static final int VIDEO_FILE_SELECT_CODE = 101;

    private Button mOpenVideoFile;
    private Button mOpenAudioFile;
    private Button mMergeAVFile;

    private TextView mAudioFile;
    private TextView mVideoFile;
    private String src_audio_input_file;
    private String src_video_input_file;
    private final String dst_av_put_file = Environment.getExternalStorageDirectory() + File.separator + "filefilm" + File.separator + "xiaokaxiu.mp4";;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        mOpenAudioFile = findViewById(R.id.open_audio_file);
        mOpenAudioFile.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                Log.d(TAG, "onClick: Audio");
                showFileChooser(AUDIO_FILE_SELECT_CODE);
            }
        });

        mOpenVideoFile = findViewById(R.id.open_video_file);
        mOpenVideoFile.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                Log.d(TAG, "onClick: Video");
                showFileChooser(VIDEO_FILE_SELECT_CODE);
            }
        });

        mMergeAVFile = findViewById(R.id.merge_av);
        mMergeAVFile.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if(null != src_audio_input_file && null != src_video_input_file) {
                    mergeAudioVideoFile(src_audio_input_file, src_video_input_file, dst_av_put_file, 5, 25, 10, 30);
                    Toast.makeText(MainActivity.this, "新音视频已合成，存放至：" + dst_av_put_file, Toast.LENGTH_LONG).show();
                } else {
                    Toast.makeText(MainActivity.this, "音视频输入文件不能为空", Toast.LENGTH_LONG).show();
                }

            }
        });

        mAudioFile = findViewById(R.id.audio_file_name);
        mVideoFile = findViewById(R.id.video_file_name);

        requestMyPermissions();
    }

    @Override
    protected void onResume() {
        super.onResume();
    }

    private void showFileChooser(int selectcode) {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.setType("*/*");
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        startActivityForResult(Intent.createChooser(intent, "Select a File to Open"), selectcode);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, @Nullable Intent data) {
        switch (requestCode) {
            case AUDIO_FILE_SELECT_CODE:
                if (resultCode == RESULT_OK) {
                    // Get the Uri of the selected file
                    Uri uri = data.getData();
                    src_audio_input_file = GetPathFromUri.getPath(this, uri);
                    mAudioFile.setText(filePathToName(src_audio_input_file));
                }
                break;
            case VIDEO_FILE_SELECT_CODE:
                if (resultCode == RESULT_OK) {
                    // Get the Uri of the selected file
                    Uri uri = data.getData();
                    src_video_input_file = GetPathFromUri.getPath(this, uri);
                    mVideoFile.setText(filePathToName(src_video_input_file));
                }
                break;
            default:
                    break;
        }
        super.onActivityResult(requestCode, resultCode, data);
    }

    private void requestMyPermissions() {

        if (ContextCompat.checkSelfPermission(this,
                Manifest.permission.WRITE_EXTERNAL_STORAGE)
                != PackageManager.PERMISSION_GRANTED) {
            //没有授权，编写申请权限代码
            ActivityCompat.requestPermissions(MainActivity.this, new String[]{Manifest.permission.WRITE_EXTERNAL_STORAGE}, 100);
        } else {
            Log.d(TAG, "requestMyPermissions: 有写SD权限");
        }
        if (ContextCompat.checkSelfPermission(this,
                Manifest.permission.READ_EXTERNAL_STORAGE)
                != PackageManager.PERMISSION_GRANTED) {
            //没有授权，编写申请权限代码
            ActivityCompat.requestPermissions(MainActivity.this, new String[]{Manifest.permission.READ_EXTERNAL_STORAGE}, 100);
        } else {
            Log.d(TAG, "requestMyPermissions: 有读SD权限");
        }
    }

    String filePathToName(String path) {
        String file_name = "";
        int index = path.lastIndexOf("/");
        if(index > 0) {
            file_name = path.substring(index + 1);
        }
        return file_name;
    }
    /**
     * A native method that is implemented by the 'native-lib' native library,
     * which is packaged with this application.
     */
    //src_audio_file: mp4, src_video_file: mp4, dst_av_file: 小咖秀输出视频
    public native void mergeAudioVideoFile(String src_audio_file, String src_video_file, String dst_av_file,
                                           int audio_start_time, int audio_stop_time, int video_start_time, int video_stop_time);
}
