from dataclasses import dataclass
import socket
import subprocess
import random
from pathlib import Path
import uuid
from tests.utils import MessageHeader, MessageType, EXE_PATH, TEST_DIR, CHECK_SIZE
import pytest

PORT = 8000
SERVER_TEST_DIR = TEST_DIR / "server"


@dataclass
class FileInfo:
    path: Path
    size: int


@pytest.fixture()
def server():
    subprocess.call(f"pkill -f {EXE_PATH.name}", shell=True)
    proc = subprocess.Popen(
        [str(EXE_PATH), "-p", str(PORT), "-m", str(SERVER_TEST_DIR)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    try:
        # Allow some time for the server to start
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        pass  # Server is still running
    yield proc

    proc.terminate()
    proc.wait()

    # Print server output
    stdout, stderr = proc.communicate()
    print("Server stdout:\n", stdout.decode())
    print("Server stderr:\n", stderr.decode())


@pytest.fixture()
def file(size: int):
    SERVER_TEST_DIR.mkdir(parents=True, exist_ok=True)
    file_path = Path(str(uuid.uuid4()))
    with open(SERVER_TEST_DIR / file_path, "wb") as f:
        f.write(random.randbytes(size))
    yield FileInfo(file_path, size)
    (SERVER_TEST_DIR / file_path).unlink(missing_ok=True)


def test_server_running(server: subprocess.Popen[bytes]) -> None:
    # Start the server process
    """Test if the server is running on localhost:8000."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        result = s.connect_ex(("localhost", PORT))
        assert result == 0, "Server is not running on localhost:8000"


def test_server_heartbeat(server: subprocess.Popen[bytes]) -> None:
    """Test if the server responds to a heartbeat message."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect(("localhost", PORT))
        id = int.from_bytes(random.randbytes(8), "big")
        header = MessageHeader(MessageType.HEARTBEAT, id)
        s.sendall(header.pack())

        print("Sent heartbeat with id:", id)

        s.settimeout(1.0)
        data = s.recv(MessageHeader.SIZE)
        response_header = MessageHeader.unpack(data)

        assert (
            response_header.type == MessageType.HEARTBEAT
        ), "Server did not respond to heartbeat correctly"

        assert (
            response_header.length == header.length
        ), "Heartbeat response length does not match request length"


@pytest.mark.parametrize("size", [512, 1024, 1024 * 10, 1024 * 1024, 1024 * 1024 * 10])
def test_server_read(
    server: subprocess.Popen[bytes],
    file: FileInfo,
):
    """Test if the server can read a file correctly."""
    with open(SERVER_TEST_DIR / file.path, "rb") as f, socket.socket(
        socket.AF_INET, socket.SOCK_STREAM
    ) as s:
        s.connect(("localhost", PORT))

        # Send READ message
        path_bytes = str(file.path).encode()
        write_path_header = MessageHeader(MessageType.MSG_READ, len(path_bytes))
        s.sendall(write_path_header.pack() + path_bytes)

        # Receive header
        data = s.recv(MessageHeader.SIZE)
        response_header = MessageHeader.unpack(data)
        assert (
            response_header.type == MessageType.MSG_READ
        ), "Server did not respond with READ message"
        assert (
            response_header.length == file.size
        ), "Server reported incorrect file size"

        # Receive file data and compare in chunks
        bytes_received = 0
        while bytes_received < file.size:
            chunk_size = min(CHECK_SIZE, file.size - bytes_received)
            data = s.recv(chunk_size, socket.MSG_WAITALL)
            file_data = f.read(chunk_size)
            assert data == file_data, "File data does not match"
            bytes_received += len(data)
