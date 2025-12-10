import enum
from dataclasses import dataclass
import struct
from pathlib import Path

EXE_PATH = Path("./build/bin/myfs")
TEST_DIR = Path("/home/vscode/tests/")
CHECK_SIZE = 4096
CHUNK_SIZE = 1048576


class MessageType(enum.Enum):
    MSG_READ = 0
    MSG_WRITE_PATH = 1
    MSG_WRITE = 2
    HEARTBEAT = 3


@dataclass
class MessageHeader:
    type: MessageType
    length: int

    FORMAT = "IQ"  # I: unsigned int (4 bytes), Q: unsigned long long (8 bytes)
    SIZE = struct.calcsize(FORMAT)

    def pack(self) -> bytes:
        return struct.pack(self.FORMAT, self.type.value, self.length)

    @classmethod
    def unpack(cls, data: bytes) -> "MessageHeader":
        type, length = struct.unpack(cls.FORMAT, data)
        return cls(MessageType(type), length)
