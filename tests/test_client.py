import socket
import pytest
from tests.utils import (
    CHUNK_SIZE,
    MessageHeader,
    MessageType,
    EXE_PATH,
    TEST_DIR,
    CHECK_SIZE,
)
import subprocess
import random
from pathlib import Path
import uuid
from dataclasses import dataclass
import shutil
from datetime import datetime
import threading
import logging
from typing import Any, MutableMapping

CLIENT_TEST_DIR = TEST_DIR / "client"
MOUNT_DIR = CLIENT_TEST_DIR / "mnt"
ROOT_DIR = CLIENT_TEST_DIR / "root"
START_PORT = 9000


# WARNING
# This shit is annoying asf, because python is also using the same syscalls we are implementing
# it hangs if there are bugs or python exits early due to assertion failures
# I set up a backup logging for troubleshooting if pytest fails
# useful command to stop fuser and kill python: umount -l ./mnt; kill -9 $(lsof -t -i:9000)


@dataclass
class FileInfo:
    path: Path
    size: int


class FlushingHandler(logging.Handler):
    """Custom handler that flushes after every log message (no buffering)"""

    def __init__(self, filename: str):
        super().__init__()
        self.file = open(filename, "a", buffering=1)  # Line buffering

    def emit(self, record: logging.LogRecord):
        try:
            msg = self.format(record)
            self.file.write(msg + "\n")
            self.file.flush()  # Instantly flush
        except Exception:
            self.handleError(record)

    def close(self):
        if self.file:
            self.file.flush()
            self.file.close()
        super().close()


class LoggerAdapter(logging.LoggerAdapter[logging.Logger]):
    """Custom adapter for contextual logging"""

    def process(self, msg: str, kwargs: MutableMapping[str, Any]):
        # Add context info to every log message
        if self.extra is None:
            return msg, kwargs
        context = self.extra.get("context", "GENERAL")
        thread_name = threading.current_thread().name
        return f"[{context}][{thread_name}] {msg}", kwargs


@pytest.fixture(autouse=True, scope="module")
def logs():
    """Setup logger with instant flushing and better formatting"""
    logger = logging.getLogger("test_client")
    logger.setLevel(logging.DEBUG)

    # File handler with instant flushing
    CLIENT_TEST_DIR.mkdir(parents=True, exist_ok=True)
    log_file = CLIENT_TEST_DIR / "test_client.log"
    file_handler = FlushingHandler(str(log_file))
    file_handler.setLevel(logging.DEBUG)
    file_formatter = logging.Formatter(
        "%(asctime)s | %(levelname)-8s | %(name)s | %(message)s",
        datefmt="%Y-%m-%d %H:%M:%S",
    )
    file_handler.setFormatter(file_formatter)
    logger.addHandler(file_handler)

    return logger


@pytest.fixture()
def connections(sockets: int, logs: logging.Logger):
    logs.info(f"Setting up {sockets} socket connection(s)")
    subprocess.call(f"fusermount -u {MOUNT_DIR}", shell=True)
    socks = [socket.socket(socket.AF_INET, socket.SOCK_STREAM) for _ in range(sockets)]
    for i, sock in enumerate(socks):
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind(("localhost", START_PORT + i))
        sock.listen()
        logs.debug(f"Socket {i} listening on localhost:{START_PORT + i}")

    # empty dirs and recreate
    shutil.rmtree(MOUNT_DIR, ignore_errors=True)
    shutil.rmtree(ROOT_DIR, ignore_errors=True)
    ROOT_DIR.mkdir(parents=True)
    MOUNT_DIR.mkdir(parents=True)

    # start process and connect sockets
    timestamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    server_log = CLIENT_TEST_DIR / f"{timestamp}_{uuid.uuid4()}.log"
    logs.info(f"Starting client process with log: {server_log}")

    proc = subprocess.Popen(
        [
            str(EXE_PATH),
            "-c",
            "-r",
            str(ROOT_DIR),
            "-m",
            str(MOUNT_DIR),
            "--servers",
            ",".join(
                f"{addr}:{port}"
                for addr, port in [sock.getsockname() for sock in socks]
            ),
            "-l",
            str(server_log),
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    try:
        proc.wait(timeout=1)
    except subprocess.TimeoutExpired:
        logs.debug("Process still running (expected)")

    cons = [sock.accept()[0] for sock in socks]
    logs.info(f"Accepted {len(cons)} client connection(s)")
    yield cons

    # clean up
    logs.info("Tearing down connections")
    proc.terminate()
    proc.wait()
    for sock in cons:
        sock.close()
    for sock in socks:
        sock.close()

    subprocess.call(f"fusermount -u {MOUNT_DIR}", shell=True)
    stdout, stderr = proc.communicate()
    if stdout:
        logs.info(f"Client process stdout:\n{stdout.decode()}")
    if stderr:
        logs.warning(f"Client process stderr:\n{stderr.decode()}")
    logs.info("Teardown complete")


@pytest.fixture()
def file(size: int):
    CLIENT_TEST_DIR.mkdir(parents=True, exist_ok=True)
    file_path = Path(str(uuid.uuid4()))
    with open(CLIENT_TEST_DIR / file_path, "wb") as f:
        f.write(random.randbytes(size))
    yield FileInfo(file_path, size)
    (CLIENT_TEST_DIR / file_path).unlink(missing_ok=True)


@pytest.mark.parametrize("sockets,size", [(1, 0), (1, 45678)])
def test_client_stat(
    connections: list[socket.socket], file: FileInfo, logs: logging.Logger
):
    logger = LoggerAdapter(logs, {"context": "STAT_TEST"})

    logger.info(f"Testing stat for file {file.path} (size: {file.size} bytes)")
    shutil.copy(CLIENT_TEST_DIR / file.path, ROOT_DIR / file.path)

    # stat mount
    stat = (MOUNT_DIR / file.path).stat()
    logger.debug(f"Got stat: size={stat.st_size}, mode={oct(stat.st_mode)}")
    assert stat.st_size == file.size
    logger.info("✓ Stat test passed")


def xor(a: bytes, sz: int) -> bytes:
    """XOR bytes a of size sz, return result as bytes"""
    result = bytearray(a[0:sz])
    for i in range(sz, len(a), sz):
        for j in range(sz):
            result[j] ^= a[i + j]
    return bytes(result)


@pytest.mark.parametrize(
    "sockets,size,fails",
    [
        (1, CHUNK_SIZE, []),
        (1, CHUNK_SIZE * 4, []),
        (1, CHUNK_SIZE * 400, []),
        (2, CHUNK_SIZE, []),
        (2, CHUNK_SIZE, [0]),
        (2, CHUNK_SIZE, [1]),
        (3, CHUNK_SIZE * 2, []),
        (3, CHUNK_SIZE * 2, [2]),
        (3, CHUNK_SIZE * 2, [1]),
        (3, CHUNK_SIZE * 2, [0]),
        (3, CHUNK_SIZE * 10, [2]),
        (3, CHUNK_SIZE * 10, [1]),
        (3, CHUNK_SIZE * 10, [0]),
        (3, CHUNK_SIZE * 400, [2]),
        (3, CHUNK_SIZE * 400, [1]),
        (3, CHUNK_SIZE * 400, [0]),
    ],
)
def test_client_read(
    connections: list[socket.socket],
    file: FileInfo,
    logs: logging.Logger,
    fails: list[int],
):
    n = len(connections)
    assert len(fails) <= 1, "NOT IMPLEMENTED: multiple connection failures"
    assert (
        n == 1 or file.size % ((n - 1) * CHUNK_SIZE) == 0
    ), "file size must be multiple of ((n - 1) * CHUNK_SIZE) for this test"

    logger = LoggerAdapter(logs, {"context": "READ_TEST"})
    logger.info(f"Starting read test for file {file.path} (size: {file.size} bytes)")

    shutil.copy(CLIENT_TEST_DIR / file.path, ROOT_DIR / file.path)
    logger.debug(f"Copied {file.path} to {ROOT_DIR / file.path}")

    for fail in fails:
        logger.debug(f"Closing connection {fail} to simulate failure")
        connections[fail].close()

    conns = [(conn, i) for i, conn in enumerate(connections) if i not in fails]

    # Server thread that responds to requests
    def server_handler():
        server_logger = LoggerAdapter(logs, {"context": "SERVER_THREAD"})
        try:
            server_logger.debug("Waiting for all client request headers")
            cnt = 0
            for conn, _ in conns:
                data = conn.recv(MessageHeader.SIZE, socket.MSG_WAITALL)
                header = MessageHeader.unpack(data)

                server_logger.debug(
                    f"Received MSG_READ with header length: {header.length}"
                )
                assert header.type == MessageType.MSG_READ

                data = conn.recv(header.length, socket.MSG_WAITALL)
                server_logger.debug(f"Received read request for path: {data.decode()}")
                remote_path = Path(data.decode().lstrip("/"))
                assert remote_path == file.path

                # Send response header
                response_header = MessageHeader(
                    MessageType.MSG_READ, file.size // (n - 1 or 1)
                )
                conn.sendall(response_header.pack())
                server_logger.debug(
                    f"Sent response header with file size: {file.size// (n - 1 or 1)}"
                )
                cnt += 1
                if cnt == n - 1:
                    break

            with open(CLIENT_TEST_DIR / file.path, "rb") as f_server:
                stride = CHUNK_SIZE * (n - 1)
                if stride == 0:
                    stride = CHUNK_SIZE
                bytes_read = 0
                while data := f_server.read(stride):
                    bytes_read += len(data)
                    for conn, idx in conns:
                        if idx == n - 1:
                            conn.sendall(xor(data, CHUNK_SIZE))
                        else:
                            conn.sendall(
                                data[idx * CHUNK_SIZE : (idx + 1) * CHUNK_SIZE]
                            )
                    server_logger.debug(f"Sent {bytes_read} bytes of file data")

        except Exception as e:
            server_logger.error(f"Server handler error: {e}", exc_info=True)
            raise

    logger.debug("Starting server handler thread")
    server_thread = threading.Thread(target=server_handler, name="server_handler")
    server_thread.start()

    logger.debug("Client reading from FUSE mount")
    # Client reads from FUSE mount
    try:
        with open(MOUNT_DIR / file.path, "rb") as f_client, open(
            CLIENT_TEST_DIR / file.path, "rb"
        ) as f_expected:

            logger.debug("Comparing client data with expected data")
            bytes_read = 0
            while bytes_read < file.size:
                chunk_size = min(CHECK_SIZE, file.size - bytes_read)
                logger.debug(f"Reading {chunk_size} bytes at offset {bytes_read}")
                client_data = f_client.read(chunk_size)
                expected_data = f_expected.read(chunk_size)
                assert (
                    client_data == expected_data
                ), f"Data mismatch between client and expected at offset {bytes_read}+{chunk_size}"
                bytes_read += len(client_data)
                logger.debug(f"Read and verified {bytes_read}/{file.size} bytes")

    except Exception as e:
        logger.error(f"Client read error: {e}", exc_info=True)
        raise

    # Wait for server thread to finish
    logger.debug("Waiting for server thread to complete")
    server_thread.join(timeout=5)
    if server_thread.is_alive():
        logger.warning("Server thread did not complete in time")
