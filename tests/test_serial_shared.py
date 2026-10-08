"""Protocol responses must survive shell prompt redraw and unrelated logs."""
import io
import sys
import unittest
import tempfile
from unittest.mock import patch
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import aster_send

class Stream:
    def __init__(self, data): self.data = io.BytesIO(data)
    @property
    def in_waiting(self): return 64
    def read(self, count): return self.data.read(count)

class SharedSerial(unittest.TestCase):
    def test_log_and_ansi_do_not_hide_ack(self):
        stream = Stream(b'\x1b[1;32master:~$ ASTERPUT\n'
                        b'[00:00:01] <inf> audio: OK ready\n'
                        b'\x1b[0mACK 512\r\n')
        self.assertEqual(aster_send._token(stream, 1), 'ACK 512')

    def test_final_verdict_with_prompt_redraw(self):
        stream = Stream(b'\x1b[9D\x1b[JOK\r\n')
        self.assertEqual(aster_send._token(stream, 1), 'OK')

    def test_probe_identity_is_preserved(self):
        self.assertEqual(aster_send.protocol_line(
            b'\x1b[32master:~$ \x1b[mPONG sd=1 target=lckfb-s3 serial=shared\r'),
            'PONG sd=1 target=lckfb-s3 serial=shared')

    def test_binary_upload_reuses_connection_and_lf_header(self):
        class Device:
            def __init__(self):
                self.pending = bytearray()
                self.writes = []
                self.received = 0
            @property
            def in_waiting(self): return len(self.pending)
            def reset_input_buffer(self): self.pending.clear()
            def flush(self): pass
            def read(self, count):
                result = bytes(self.pending[:count])
                del self.pending[:count]
                return result
            def write(self, data):
                self.writes.append(data)
                if data == b'ASTERPING\n':
                    self.pending.extend(b'PONG sd=1 serial=shared\n')
                elif data.startswith(b'ASTERPUT '):
                    self.pending.extend(b'aster:~$ ASTERPUT\nACK 0\n')
                else:
                    self.received += len(data)
                    self.pending.extend(b'[00:01] <inf> app: running\n')
                    self.pending.extend(b'OK\n' if self.received == 600 else b'ACK 512\n')
        device = Device()
        payload = bytes(range(256)) * 2 + bytes(range(88))
        with tempfile.TemporaryDirectory() as directory:
            local = Path(directory) / 'binary.bin'
            local.write_bytes(payload)
            with patch.object(aster_send.time, 'sleep'), \
                 patch.object(aster_send, 'open_serial', side_effect=AssertionError('must reuse')):
                self.assertEqual(aster_send.send_file('COM5', local, '/SD:/binary.bin',
                                                     connection=device), 'OK')
        self.assertTrue(device.writes[1].endswith(b'\n'))
        self.assertFalse(device.writes[1].endswith(b'\r\n'))
        self.assertEqual(b''.join(device.writes[2:]), payload)

if __name__ == '__main__': unittest.main()
