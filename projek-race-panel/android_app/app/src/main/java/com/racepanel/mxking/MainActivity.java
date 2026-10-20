package com.racepanel.mxking;

import android.Manifest;
import android.app.Activity;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothSocket;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.webkit.JavascriptInterface;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Toast;

import java.io.BufferedReader;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.util.Set;
import java.util.UUID;

public class MainActivity extends Activity {
    private static final String TARGET_DEVICE_NAME = "RacePanel-RedLeo";
    private static final UUID SPP_UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB");

    private WebView webView;
    private BluetoothAdapter bluetoothAdapter;
    private BluetoothSocket bluetoothSocket;
    private OutputStream outputStream;
    private InputStream inputStream;
    private Thread workerThread;
    private volatile boolean isRunning = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        webView = new WebView(this);
        setContentView(webView);

        WebSettings settings = webView.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        settings.setAllowFileAccess(true);

        webView.addJavascriptInterface(new WebAppInterface(), "AndroidBridge");
        webView.setWebViewClient(new WebViewClient());
        webView.loadUrl("file:///android_asset/index.html");

        checkPermissions();
    }

    private void checkPermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            if (checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED) {
                requestPermissions(new String[]{
                        Manifest.permission.BLUETOOTH_CONNECT,
                        Manifest.permission.BLUETOOTH_SCAN
                }, 101);
            }
        }
    }

    public class WebAppInterface {
        @JavascriptInterface
        public void connectBluetooth() {
            new Thread(() -> startBluetoothConnection()).start();
        }

        @JavascriptInterface
        public void sendCommand(String cmd) {
            try {
                if (outputStream != null) {
                    outputStream.write((cmd + "\n").getBytes());
                    outputStream.flush();
                }
            } catch (Exception e) {
                e.printStackTrace();
            }
        }
    }

    private void startBluetoothConnection() {
        bluetoothAdapter = BluetoothAdapter.getDefaultAdapter();
        if (bluetoothAdapter == null || !bluetoothAdapter.isEnabled()) {
            runOnUiThread(() -> Toast.makeText(this, "Nyalakan Bluetooth HP Anda!", Toast.LENGTH_SHORT).show());
            return;
        }

        BluetoothDevice targetDevice = null;
        try {
            Set<BluetoothDevice> pairedDevices = bluetoothAdapter.getBondedDevices();
            for (BluetoothDevice device : pairedDevices) {
                if (TARGET_DEVICE_NAME.equalsIgnoreCase(device.getName())) {
                    targetDevice = device;
                    break;
                }
            }
        } catch (SecurityException e) {
            e.printStackTrace();
        }

        if (targetDevice == null) {
            runOnUiThread(() -> Toast.makeText(this, "Pairing 'RacePanel-RedLeo' di menu Bluetooth HP dulu!", Toast.LENGTH_LONG).show());
            return;
        }

        try {
            bluetoothSocket = targetDevice.createRfcommSocketToServiceRecord(SPP_UUID);
            bluetoothSocket.connect();
            outputStream = bluetoothSocket.getOutputStream();
            inputStream = bluetoothSocket.getInputStream();

            isRunning = true;
            runOnUiThread(() -> {
                Toast.makeText(this, "Terhubung ke RacePanel-RedLeo!", Toast.LENGTH_SHORT).show();
                webView.evaluateJavascript("onBluetoothStatus(true);", null);
            });

            beginListenForData();

        } catch (Exception e) {
            e.printStackTrace();
            runOnUiThread(() -> {
                Toast.makeText(this, "Gagal terhubung ke ESP32!", Toast.LENGTH_SHORT).show();
                webView.evaluateJavascript("onBluetoothStatus(false);", null);
            });
        }
    }

    private void beginListenForData() {
        workerThread = new Thread(() -> {
            BufferedReader reader = new BufferedReader(new InputStreamReader(inputStream));
            while (isRunning && !Thread.currentThread().isInterrupted()) {
                try {
                    String line = reader.readLine();
                    if (line != null && !line.isEmpty()) {
                        final String safeLine = line.replace("'", "\\'");
                        runOnUiThread(() -> webView.evaluateJavascript("onBluetoothData('" + safeLine + "');", null));
                    }
                } catch (Exception e) {
                    isRunning = false;
                    runOnUiThread(() -> webView.evaluateJavascript("onBluetoothStatus(false);", null));
                    break;
                }
            }
        });
        workerThread.start();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        isRunning = false;
        try {
            if (bluetoothSocket != null) bluetoothSocket.close();
        } catch (Exception ignored) {}
    }
}
