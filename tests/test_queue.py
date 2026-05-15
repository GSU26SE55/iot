import tempfile
import unittest
from pathlib import Path

from gateway.queue.local_store import LocalQueue


class LocalQueueTests(unittest.TestCase):
    def test_enqueue_due_and_mark_sent(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            queue = LocalQueue(Path(tmp) / "queue.sqlite3")
            message_id = queue.enqueue(kind="sensor_batch", payload={"items": []}, idempotency_key="abc")

            self.assertEqual(queue.depth(), 1)
            messages = queue.due_messages()
            self.assertEqual(len(messages), 1)
            self.assertEqual(messages[0].id, message_id)
            self.assertEqual(messages[0].idempotency_key, "abc")

            queue.mark_sent(message_id)
            self.assertEqual(queue.depth(), 0)

    def test_mark_failed_delays_retry(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            queue = LocalQueue(Path(tmp) / "queue.sqlite3")
            message_id = queue.enqueue(kind="sensor_batch", payload={"items": []}, idempotency_key="abc")

            queue.mark_failed(message_id, "backend down")

            self.assertEqual(queue.depth(), 1)
            self.assertEqual(queue.due_messages(), [])


if __name__ == "__main__":
    unittest.main()

