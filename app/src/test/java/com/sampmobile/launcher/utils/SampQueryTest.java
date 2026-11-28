package com.sampmobile.launcher.utils;

import org.junit.Test;

import java.io.ByteArrayOutputStream;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

public class SampQueryTest {

    private interface ReplyHandler {
        byte[] handle(byte[] request);
    }

    private static final class LoopbackMockServer implements AutoCloseable {
        private final DatagramSocket socket;
        private final AtomicBoolean running = new AtomicBoolean(true);
        private final Thread thread;

        LoopbackMockServer(ReplyHandler handler) throws Exception {
            this.socket = new DatagramSocket(0, InetAddress.getLoopbackAddress());
            this.socket.setSoTimeout(3000);
            this.thread = new Thread(() -> {
                byte[] buf = new byte[1024];
                while (running.get()) {
                    try {
                        DatagramPacket req = new DatagramPacket(buf, buf.length);
                        socket.receive(req);
                        byte[] copy = new byte[req.getLength()];
                        System.arraycopy(req.getData(), 0, copy, 0, req.getLength());
                        byte[] reply = handler.handle(copy);
                        if (reply != null) {
                            socket.send(new DatagramPacket(reply, reply.length, req.getAddress(), req.getPort()));
                        }
                    } catch (Exception ignored) {
                        if (!running.get()) break;
                    }
                }
            }, "samp-query-test-server");
            this.thread.setDaemon(true);
            this.thread.start();
        }

        int getPort() {
            return socket.getLocalPort();
        }

        @Override
        public void close() {
            running.set(false);
            socket.close();
            try {
                thread.join(500);
            } catch (InterruptedException ignored) {
            }
        }
    }

    private static byte[] buildInfoReply(byte[] reqHeader, boolean passworded, int players, int maxPlayers,
                                         String host, String mode, String lang, Charset cs) {
        byte[] hBytes = host.getBytes(cs);
        byte[] mBytes = mode.getBytes(cs);
        byte[] lBytes = lang.getBytes(cs);
        ByteBuffer bb = ByteBuffer.allocate(11 + 1 + 2 + 2 + 4 + hBytes.length + 4 + mBytes.length + 4 + lBytes.length)
                .order(ByteOrder.LITTLE_ENDIAN);
        bb.put(reqHeader, 0, 11);
        bb.put((byte) (passworded ? 1 : 0));
        bb.putShort((short) players);
        bb.putShort((short) maxPlayers);
        bb.putInt(hBytes.length).put(hBytes);
        bb.putInt(mBytes.length).put(mBytes);
        bb.putInt(lBytes.length).put(lBytes);
        return bb.array();
    }

    @Test
    public void isOnlineAndGetPing_verifyEchoedPingToken() throws Exception {
        try (LoopbackMockServer server = new LoopbackMockServer(req -> {
            if (req.length >= 15 && req[0] == 'S' && req[1] == 'A' && req[2] == 'M' && req[3] == 'P' && req[10] == 'p') {
                return req; // echo full 15-byte ping packet
            }
            return null;
        })) {
            SampQuery q = new SampQuery("127.0.0.1", server.getPort());
            try {
                assertTrue(q.isOnline());
                assertTrue(q.getPing() >= 0L);
            } finally {
                q.close();
            }
        }
    }

    @Test
    public void getInfo_parsesGbkAndUtf8ChineseAndClampsMalformedLength() throws Exception {
        Charset gbk = Charset.forName("GBK");
        try (LoopbackMockServer server = new LoopbackMockServer(req -> {
            if (req.length >= 11 && req[10] == 'i') {
                return buildInfoReply(req, true, 42, 200, "中文测试服", "自由模式", "中文", gbk);
            }
            return null;
        })) {
            SampQuery q = new SampQuery("127.0.0.1", server.getPort(), "GBK");
            try {
                String[] info = q.getInfo();
                assertNotNull(info);
                assertEquals("1", info[0]);
                assertEquals("42", info[1]);
                assertEquals("200", info[2]);
                assertEquals("中文测试服", info[3]);
                assertEquals("自由模式", info[4]);
                assertEquals("中文", info[5]);
            } finally {
                q.close();
            }
        }

        // Malformed huge hostname length (0x7FFFFFFF) must clamp safely without OOM
        try (LoopbackMockServer server = new LoopbackMockServer(req -> {
            if (req.length >= 11 && req[10] == 'i') {
                ByteBuffer bb = ByteBuffer.allocate(24).order(ByteOrder.LITTLE_ENDIAN);
                bb.put(req, 0, 11);
                bb.put((byte) 0);
                bb.putShort((short) 1);
                bb.putShort((short) 50);
                bb.putInt(Integer.MAX_VALUE); // malicious length
                bb.put("abc".getBytes(StandardCharsets.UTF_8));
                return bb.array();
            }
            return null;
        })) {
            SampQuery q = new SampQuery("127.0.0.1", server.getPort(), "UTF-8");
            try {
                // Clamping prevents OOM; remaining fields fail cleanly -> null (no crash)
                assertNull(q.getInfo());
            } finally {
                q.close();
            }
        }
    }

    @Test
    public void getPlayersAndRules_parseDetailedPlayersAndRules() throws Exception {
        try (LoopbackMockServer server = new LoopbackMockServer(req -> {
            if (req.length < 11) return null;
            if (req[10] == 'd') {
                ByteArrayOutputStream out = new ByteArrayOutputStream();
                out.write(req, 0, 11);
                ByteBuffer body = ByteBuffer.allocate(2 + 1 + 1 + 5 + 4 + 4).order(ByteOrder.LITTLE_ENDIAN);
                body.putShort((short) 1);
                body.put((byte) 7);
                body.put((byte) 5);
                body.put("Alice".getBytes(StandardCharsets.UTF_8));
                body.putInt(120);
                body.putInt(35);
                byte[] b = body.array();
                out.write(b, 0, b.length);
                return out.toByteArray();
            }
            if (req[10] == 'r') {
                ByteArrayOutputStream out = new ByteArrayOutputStream();
                out.write(req, 0, 11);
                ByteBuffer body = ByteBuffer.allocate(2 + 1 + 7 + 1 + 7).order(ByteOrder.LITTLE_ENDIAN);
                body.putShort((short) 1);
                body.put((byte) 7);
                body.put("version".getBytes(StandardCharsets.UTF_8));
                body.put((byte) 7);
                body.put("0.3.7R2".getBytes(StandardCharsets.UTF_8));
                byte[] b = body.array();
                out.write(b, 0, b.length);
                return out.toByteArray();
            }
            return null;
        })) {
            SampQuery q = new SampQuery("127.0.0.1", server.getPort(), "UTF-8");
            try {
                List<String[]> players = q.getPlayers();
                assertEquals(1, players.size());
                assertEquals("7", players.get(0)[0]);
                assertEquals("Alice", players.get(0)[1]);
                assertEquals("120", players.get(0)[2]);
                assertEquals("35", players.get(0)[3]);

                List<String[]> rules = q.getRules();
                assertEquals(1, rules.size());
                assertEquals("version", rules.get(0)[0]);
                assertEquals("0.3.7R2", rules.get(0)[1]);
            } finally {
                q.close();
            }
        }
    }
}
