"""Windows end-to-end checks; Python standard library only, no app dependencies."""
import argparse
import csv
import socket
import struct
import subprocess
import time
from pathlib import Path


def packet(msg_id, seq, payload=b''):
    body = struct.pack('<IHIH', 0x53454E53, msg_id, seq, len(payload)) + payload
    return body + struct.pack('<H', sum(body) & 65535)


def unpack(data):
    assert len(data) >= 14
    magic, msg_id, seq, size = struct.unpack('<IHIH', data[:12])
    assert magic == 0x53454E53 and len(data) == size + 14
    assert (sum(data[:-2]) & 65535) == struct.unpack('<H', data[-2:])[0]
    return msg_id, seq, data[12:-2]


def port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


def terminate(p):
    if p.poll() is None:
        p.terminate()
        p.wait(timeout=3)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--bin-dir', type=Path, required=True)
    ap.add_argument('--output-dir', type=Path, required=True)
    args = ap.parse_args()
    args.bin_dir = args.bin_dir.resolve()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    sensor = args.bin_dir / 'SensorSim.exe'
    collector = args.bin_dir / 'Collector.exe'
    assert sensor.is_file() and collector.is_file(), 'Applications are not built yet'

    # Real sensor against an independent wire implementation.
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as peer:
        peer.bind(('127.0.0.1', 0))
        peer.settimeout(0.1)
        sp = port()
        p = subprocess.Popen([str(sensor), '--listen-port', str(sp), '--peer-port',
                              str(peer.getsockname()[1]), '--run-for-ms', '15000'],
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        sequences = []
        heartbeat_times = []
        def collect(duration):
            result = []
            end = time.monotonic() + duration
            while time.monotonic() < end:
                try:
                    data, sender = peer.recvfrom(65535)
                    assert sender == ('127.0.0.1', sp)
                    message = unpack(data)
                    sequences.append(message[1])
                    if message[0] == 1:
                        heartbeat_times.append(time.monotonic())
                    result.append(message)
                except socket.timeout:
                    pass
            return result
        def command(msg_id, seq, payload=b'', expected=20, reason=None):
            data = packet(msg_id, seq, payload)
            peer.sendto(data, ('127.0.0.1', sp))
            until = time.monotonic() + 1.5
            while time.monotonic() < until:
                for reply_id, _, body in collect(0.1):
                    if reply_id in (20, 21) and struct.unpack('<HI', body[:6]) == (msg_id, seq):
                        assert reply_id == expected, (reply_id, expected)
                        if reason is not None:
                            assert body[6] == reason
                        return
            raise AssertionError('No matching ACK/NACK')
        try:
            initial = collect(1.3)
            assert sum(m[0] == 2 for m in initial) >= 4
            samples = [struct.unpack('<ffI', m[2]) for m in initial if m[0] == 2]
            assert all(20 <= t <= 30 and 40 <= h <= 60 for t, h, _ in samples)
            assert all(180 <= b[2]-a[2] <= 350 for a, b in zip(samples, samples[1:]))
            assert len(heartbeat_times) >= 2
            assert 0.85 <= heartbeat_times[1]-heartbeat_times[0] <= 1.2
            command(10, 1, struct.pack('<H', 49), 21, 1)
            command(10, 2, struct.pack('<H', 500))
            slow = [m for m in collect(1.7) if m[0] == 2]
            assert 2 <= len(slow) <= 4
            uptimes = [struct.unpack('<ffI', m[2])[2] for m in slow]
            assert all(420 <= b-a <= 650 for a, b in zip(uptimes, uptimes[1:]))
            command(11, 3)
            fault = collect(1.3)
            assert not any(m[0] == 2 for m in fault)
            assert any(m[0] == 1 and m[2] == b'\x02' for m in fault)
            command(10, 4, struct.pack('<H', 200), 21, 2)
            command(12, 5)
            command(11, 3)  # Delayed retry must not put the sensor back into FAULT.
            resumed = [m for m in collect(0.9) if m[0] == 2]
            assert len(resumed) >= 3
            assert struct.unpack('<ffI', resumed[0][2])[2] > uptimes[-1]
            command(12, 6, b'x', 21, 1)
            # Correct packet from a foreign source port must not control the sensor.
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as foreign:
                foreign.sendto(packet(11, 999), ('127.0.0.1', sp))
            bad = bytearray(packet(11, 7)); bad[-1] ^= 1
            peer.sendto(bad, ('127.0.0.1', sp))
            peer.sendto(b'x' * 60000, ('127.0.0.1', sp))
            assert sum(m[0] == 2 for m in collect(0.9)) >= 3
            assert sequences[0] == 1
            assert all(b == a+1 for a, b in zip(sequences, sequences[1:])), sequences
        finally:
            terminate(p)
            (args.output_dir / 'sensor-wire-test.txt').write_text(p.stdout.read(), encoding='utf-8')

    # Real Collector: retry exact same bytes, match ACKs, repair reordered loss,
    # ignore duplicates, damaged input and packets from another source port.
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as fake:
        fake.bind(('127.0.0.1', 0)); fake.settimeout(2)
        cp = port()
        csv_path = (args.output_dir / 'wire.csv').resolve()
        p = subprocess.Popen([str(collector), '--listen-port', str(cp), '--peer-port',
                              str(fake.getsockname()[1]), '--csv', str(csv_path)],
                             stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, text=True)
        try:
            time.sleep(0.3)
            p.stdin.write('rate 500\n'); p.stdin.flush()
            first, sender = fake.recvfrom(65535)
            assert sender == ('127.0.0.1', cp)
            cid, cseq, body = unpack(first)
            assert cid == 10 and body == struct.pack('<H', 500)
            fake.sendto(packet(20, 98, struct.pack('<HI', 11, cseq)), sender)
            fake.sendto(packet(20, 99, struct.pack('<HI', cid, cseq+1)), sender)
            second, _ = fake.recvfrom(65535)
            assert first == second, 'Retry must reuse the same seq and payload'
            fake.sendto(packet(20, 100, struct.pack('<HI', cid, cseq)), sender)
            for seq in (102, 101, 102, 104):
                fake.sendto(packet(2, seq, struct.pack('<ffI', 25, 50, seq)), sender)
            fake.sendto(b'bad', sender)
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as foreign:
                foreign.sendto(packet(1, 999, b'\x02'), sender)
            time.sleep(0.4)
            p.stdin.write('status\nreset\n'); p.stdin.flush()
            # Do not answer RESET: initial transmission + exactly two retries.
            reset_packets = [fake.recvfrom(65535)[0] for _ in range(3)]
            assert len(set(reset_packets)) == 1 and unpack(reset_packets[0])[0] == 12
            time.sleep(0.8)
            p.stdin.write('quit\n'); p.stdin.flush()
            output, _ = p.communicate(timeout=3)
            assert p.returncode == 0, output
            for text in ('RETRY', 'ACK', 'unsolicited', 'TIMEOUT', 'loss=1', '14.29%', 'invalid=1', 'foreign=1'):
                assert text in output, (text, output)
            rows = list(csv.DictReader(csv_path.open(newline='')))
            assert [int(row['seq']) for row in rows] == [102, 101, 104]
            (args.output_dir / 'collector-wire-test.txt').write_text(output, encoding='utf-8')
        finally:
            terminate(p)

    # Required demonstration: both real processes, actual CSV and console transcript.
    sp, cp = port(), port()
    csv_path = (args.output_dir / 'demo.csv').resolve()
    with (args.output_dir / 'demo-sensor.txt').open('w', encoding='utf-8') as log:
        s = subprocess.Popen([str(sensor), '--listen-port', str(sp), '--peer-port', str(cp),
                              '--run-for-ms', '12000'], stdout=log, stderr=subprocess.STDOUT)
        p = subprocess.Popen([str(collector), '--listen-port', str(cp), '--peer-port', str(sp),
                              '--csv', str(csv_path)], stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        try:
            time.sleep(1.3)
            for line, delay in [('status', .1), ('rate 500', 1.8), ('fault', 1.5),
                                ('rate 200', .2), ('reset', 1.1), ('status', .1), ('quit', 0)]:
                p.stdin.write(line + '\n'); p.stdin.flush(); time.sleep(delay)
            output, _ = p.communicate(timeout=3)
            assert p.returncode == 0, output
            for text in ('SAMPLE', 'HEARTBEAT status=FAULT', 'ACK cmd=10', 'ACK cmd=11',
                         'ACK cmd=12', 'NACK cmd=10', 'reason=BUSY'):
                assert text in output, (text, output)
            rows = list(csv.DictReader(csv_path.open(newline='')))
            assert len(rows) >= 9
            (args.output_dir / 'demo-collector.txt').write_text(output, encoding='utf-8')
        finally:
            terminate(p); terminate(s)
    # CLI rejects unsafe/non-loopback configuration and malformed numbers.
    for app, opts in [(sensor, ['--period-ms', '49']), (sensor, ['--period-ms', '200x']),
                      (collector, ['--peer-address', '8.8.8.8']),
                      (collector, ['--listen-port', '0'])]:
        r = subprocess.run([str(app), *opts], capture_output=True, timeout=3)
        assert r.returncode != 0, opts
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as occupied:
        occupied.bind(('127.0.0.1', 0))
        r = subprocess.run([str(sensor), '--listen-port', str(occupied.getsockname()[1]),
                            '--peer-port', str(port()), '--run-for-ms', '100'],
                           capture_output=True, timeout=3)
        assert r.returncode != 0 and b'bind' in r.stderr
    r = subprocess.run([str(collector), '--listen-port', str(port()), '--peer-port', str(port()),
                        '--csv', str(args.output_dir / 'missing-directory' / 'log.csv')],
                       stdin=subprocess.DEVNULL, capture_output=True, timeout=3)
    assert r.returncode != 0, 'CSV creation failure must be fatal'
    r = subprocess.run([str(collector), '--listen-port', str(port()), '--peer-port', str(port()),
                        '--csv', str(args.output_dir / 'eof.csv')],
                       stdin=subprocess.DEVNULL, capture_output=True, timeout=3)
    assert r.returncode == 0 and b'Stopped' in r.stdout, 'EOF shutdown'
    for app in (sensor, collector):
        opts = ['--listen-port', str(port()), '--peer-port', str(port()), '--run-for-ms', '100']
        if app == collector:
            opts += ['--csv', str(args.output_dir / 'timed.csv')]
        p = subprocess.Popen([str(app), *opts], stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        try:
            assert p.wait(timeout=3) == 0, 'Timed shutdown must not wait for stdin'
        finally:
            terminate(p)
            p.communicate()
    print('PASS: sensor wire/state/timing/seq, collector ACK matching/retries/loss/CSV, '
          'two-process demo, CLI, port/CSV errors, EOF and timed shutdown')


if __name__ == '__main__':
    main()
