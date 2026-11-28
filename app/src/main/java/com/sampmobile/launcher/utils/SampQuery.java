package com.sampmobile.launcher.utils;

import com.joom.paranoid.Obfuscate;

import java.io.IOException;
import java.io.UnsupportedEncodingException;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.SocketException;
import java.net.UnknownHostException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Random;

@Obfuscate
public class SampQuery {

    private DatagramSocket socket = null;
    private InetAddress serverip = null;
    private String serveraddress = "";
    private final int serverport;
    private final Random f = new Random();
    private boolean isValidAddr = true;
    private final String charset;
    private final Random random = new Random();

    public SampQuery(String srvip, int srvport) {
        this(srvip, srvport, "GBK");
    }

    public SampQuery(String srvip, int srvport, String charset) {
        this.charset = charset;
        this.serverport = srvport;
        try {
            serverip = InetAddress.getByName(srvip);
            serveraddress = serverip.getHostAddress();
        } catch (UnknownHostException e) {
            this.isValidAddr = false;
            serveraddress = null;
        }

        try {
            socket = new DatagramSocket();
            socket.setSoTimeout(2000);
        } catch (SocketException e) {
            this.isValidAddr = false;
            serveraddress = null;
        }
    }

    public boolean isOnline() {
        if (isValidAddr && socket != null) {
            byte[] randomBytes = generateRandomBytes();
            sendPacket(initPacket((byte) 'p', randomBytes));
            byte[] data = receiveData();

            if (data != null && data.length >= 15 &&
                    data[10] == 'p' && data[11] == randomBytes[0] &&
                    data[12] == randomBytes[1] && data[13] == randomBytes[2]) {
                return data[14] == randomBytes[3];
            }
        }
        return false;
    }

    private byte[] generateRandomBytes() {
        byte[] randomBytes = new byte[4];
        random.nextBytes(randomBytes);
        randomBytes[0] = (byte) (randomBytes[0] % 100 + 110);
        randomBytes[1] = (byte) (randomBytes[1] % 128);
        randomBytes[2] = (byte) (randomBytes[2] % 128);
        randomBytes[3] = (byte) (randomBytes[3] % 50);
        return randomBytes;
    }

    public String[] getInfo() {
        String[] infos = new String[6];
        try {
            sendPacket(initPacket("i"));
            byte[] data = receiveData();
            // A socket timeout returns a zeroed buffer - require the reply opcode
            // so a missing response is reported as failure instead of empty info.
            if (data == null || data.length <= 11 || data[10] != 'i') {
                return null;
            }
            ByteBuffer buff = ByteBuffer.wrap(data);
            buff.order(ByteOrder.LITTLE_ENDIAN);
            buff.position(11);

            // Password
            infos[0] = String.valueOf(buff.get() == 1 ? 1 : 0);

            // Players
            infos[1] = String.valueOf(buff.getShort());

            // Max Players
            infos[2] = String.valueOf(buff.getShort());

            // Hostname
            int hostnamelength = buff.getInt();
            infos[3] = convert(buff, hostnamelength);

            // Gamemode
            int gamemodelength = buff.getInt();
            infos[4] = convert(buff, gamemodelength);

            // Language
            int languagelength = buff.getInt();
            infos[5] = convert(buff, languagelength);

            return infos;
        } catch (Exception e) {
            return null;
        }
    }

    private DatagramPacket initPacket(String type) {
        if (type == null || type.isEmpty()) return null;
        return initPacket((byte) type.charAt(0), null);
    }

    private DatagramPacket initPacket(byte opcode, byte[] extra) {
        try {
            if (serverip == null) return null;
            byte[] ipBytes = serverip.getAddress();
            if (ipBytes == null || ipBytes.length != 4) return null;
            int extraLen = extra != null ? extra.length : 0;
            byte[] buf = new byte[11 + extraLen];
            buf[0] = 'S';
            buf[1] = 'A';
            buf[2] = 'M';
            buf[3] = 'P';
            buf[4] = ipBytes[0];
            buf[5] = ipBytes[1];
            buf[6] = ipBytes[2];
            buf[7] = ipBytes[3];
            buf[8] = (byte) (serverport & 0xFF);
            buf[9] = (byte) ((serverport >> 8) & 0xFF);
            buf[10] = opcode;
            if (extraLen > 0) {
                System.arraycopy(extra, 0, buf, 11, extraLen);
            }
            return new DatagramPacket(buf, buf.length, serverip, serverport);
        } catch (Exception e) {
            return null;
        }
    }

    private byte[] receiveData() {
        if (socket == null) {
            return null;
        }
        byte[] data = new byte[3072];
        try {
            DatagramPacket getpacket = new DatagramPacket(data, data.length);
            socket.receive(getpacket);
            int len = getpacket.getLength();
            if (len <= 0) {
                return null;
            }
            return Arrays.copyOf(data, Math.min(len, data.length));
        } catch (IOException ignored) {
            return null;
        }
    }

    private void sendPacket(DatagramPacket d) {
        try {
            if (socket != null && d != null) {
                socket.send(d);
            }
        } catch (IOException ignored) {
        }
    }

    public void close() {
        if (socket != null) {
            socket.close();
        }
    }

    public long getPing() {
        return e();
    }

    public List<String[]> getPlayers() {
        List<String[]> players = new ArrayList<>();
        try {
            sendPacket(initPacket("d"));
            byte[] data = receiveData();
            if (data == null || data.length <= 13 || data[10] != 'd') {
                return getPlayersLite();
            }
            ByteBuffer buff = ByteBuffer.wrap(data);
            buff.order(ByteOrder.LITTLE_ENDIAN);
            buff.position(11);
            int count = buff.getShort() & 0xFFFF;
            for (int i = 0; i < count; i++) {
                try {
                    int id = buff.get() & 0xFF;
                    int nameLen = buff.get() & 0xFF;
                    byte[] nameBytes = new byte[nameLen];
                    buff.get(nameBytes);
                    int score = buff.getInt();
                    int ping = buff.getInt();
                    players.add(new String[]{String.valueOf(id), decode(nameBytes), String.valueOf(score), String.valueOf(ping)});
                } catch (Exception e) {
                    break;
                }
            }
        } catch (Exception e) {
            e.printStackTrace();
        }
        return players;
    }

    private List<String[]> getPlayersLite() {
        List<String[]> players = new ArrayList<>();
        try {
            sendPacket(initPacket("c"));
            byte[] data = receiveData();
            if (data == null || data.length <= 13 || data[10] != 'c') {
                return players;
            }
            ByteBuffer buff = ByteBuffer.wrap(data);
            buff.order(ByteOrder.LITTLE_ENDIAN);
            buff.position(11);
            int count = buff.getShort() & 0xFFFF;
            for (int i = 0; i < count; i++) {
                try {
                    int nameLen = buff.get() & 0xFF;
                    byte[] nameBytes = new byte[nameLen];
                    buff.get(nameBytes);
                    players.add(new String[]{String.valueOf(i), decode(nameBytes), "-", "-"});
                } catch (Exception e) {
                    break;
                }
            }
        } catch (Exception e) {
            e.printStackTrace();
        }
        return players;
    }

    public List<String[]> getRules() {
        List<String[]> rules = new ArrayList<>();
        try {
            sendPacket(initPacket("r"));
            byte[] data = receiveData();
            if (data == null || data.length <= 13 || data[10] != 'r') {
                return rules;
            }
            ByteBuffer buff = ByteBuffer.wrap(data);
            buff.order(ByteOrder.LITTLE_ENDIAN);
            buff.position(11);
            int count = buff.getShort() & 0xFFFF;
            for (int i = 0; i < count; i++) {
                try {
                    int nameLen = buff.get() & 0xFF;
                    byte[] nameBytes = new byte[nameLen];
                    buff.get(nameBytes);
                    int valueLen = buff.get() & 0xFF;
                    byte[] valueBytes = new byte[valueLen];
                    buff.get(valueBytes);
                    rules.add(new String[]{decode(nameBytes), decode(valueBytes)});
                } catch (Exception e) {
                    break;
                }
            }
        } catch (Exception e) {
            e.printStackTrace();
        }
        return rules;
    }

    public long e() {
        if (!isValidAddr || socket == null) {
            return -1L;
        }
        byte[] pingToken = this.f();
        DatagramPacket pkt = initPacket((byte) 'p', pingToken);
        if (pkt == null) {
            return -1L;
        }
        long startMs = System.currentTimeMillis();
        sendPacket(pkt);
        byte[] reply = receiveData();
        long endMs = System.currentTimeMillis();
        if (reply == null || reply.length < 15 || reply[10] != 'p'
                || reply[11] != pingToken[0] || reply[12] != pingToken[1]
                || reply[13] != pingToken[2] || reply[14] != pingToken[3]) {
            return -1L;
        }
        return Math.max(0L, endMs - startMs);
    }

    byte[] f() {
        byte[] var1 = new byte[4];
        this.f.nextBytes(var1);
        var1[0] = (byte)(var1[0] % 100 + 110 & 255);
        var1[1] = (byte)(var1[1] % 128);
        var1[2] = (byte)(var1[2] % 128);
        var1[3] = (byte)(var1[3] % 50 & 255);
        return var1;
    }

    private String convert(ByteBuffer buff, int length) throws UnsupportedEncodingException {
        // Clamp server-supplied length to the packet contents - a malformed packet
        // must not trigger a huge allocation (OOM) or out-of-bounds reads.
        if (length < 0) length = 0;
        if (length > buff.remaining()) length = buff.remaining();
        byte[] n = new byte[length];
        try{
            for (int x = 0; x < length; x++)
                n[x] = buff.get();
        }
        catch (Exception ignored) {
        }
        return decode(n);
    }

    private String decode(byte[] bytes) {
        if (!"UTF-8".equalsIgnoreCase(charset)) {
            try {
                return StandardCharsets.UTF_8.newDecoder()
                        .onMalformedInput(CodingErrorAction.REPORT)
                        .onUnmappableCharacter(CodingErrorAction.REPORT)
                        .decode(ByteBuffer.wrap(bytes)).toString();
            } catch (CharacterCodingException ignored) {
            }
        }
        try {
            return new String(bytes, charset);
        } catch (UnsupportedEncodingException e) {
            return new String(bytes, StandardCharsets.UTF_8);
        }
    }
}