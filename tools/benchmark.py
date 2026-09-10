#!/usr/bin/env python3
"""Local benchmark client for the existing SearchEngine TCP server.

The wire format intentionally matches src/module4/client.cc and
src/module3/TcpConnection.cc exactly: native unsigned size_t length followed
by a UTF-8 JSON body.  It is therefore a same-host/compatible-architecture
test tool, not a portable network protocol implementation.
"""

from __future__ import annotations

import argparse
import asyncio
import json
import math
import statistics
import struct
import sys
import time
from dataclasses import dataclass, field
from typing import Sequence


# C++ sends sizeof(size_t) bytes without byte-order conversion.  '@N' is the
# native unsigned size_t representation, which is correct for this WSL2 host.
SIZE_T = struct.Struct("@N")
EXPECTED_SUCCESS_RESPONSE_IDS = {1: 100, 2: 200}


@dataclass
class WorkerResult:
    success: int = 0
    errors: int = 0
    business_success: int = 0
    business_miss: int = 0
    abnormal_responses: int = 0
    response_msg_ids: dict[int, int] = field(default_factory=dict)
    latencies_ms: list[float] = field(default_factory=list)
    samples: list[str] = field(default_factory=list)


@dataclass
class Session:
    reader: asyncio.StreamReader
    writer: asyncio.StreamWriter


@dataclass
class Response:
    latency_ms: float
    msg_id: int
    pretty_json: str


@dataclass
class SampleRecorder:
    response: str | None = None

    def record(self, response: str) -> None:
        # This assignment has no await point, so the first completed worker wins.
        if self.response is None:
            self.response = response


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Local asyncio benchmark for the existing SearchEngine TCP protocol."
    )
    parser.add_argument("--host", default="127.0.0.1", help="server host (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=1234, help="server port (default: 1234)")
    parser.add_argument("-c", "--concurrency", type=int, default=1, help="concurrent TCP connections")
    parser.add_argument(
        "-n", "--requests-per-connection", type=int, default=10,
        help="measured requests sent sequentially on each connection",
    )
    parser.add_argument("--msg-id", type=int, choices=(1, 2), default=1, help="1=keyword, 2=web search")
    parser.add_argument("--query", default="linux", help="value of the JSON msg field")
    parser.add_argument("--warmup", type=int, default=0, help="requests per connection excluded from metrics")
    parser.add_argument(
        "--show-sample", action="store_true",
        help="print the first complete measured business response",
    )
    parser.add_argument("--timeout", type=float, default=10.0, help="per-request timeout in seconds")
    parser.add_argument(
        "--max-response-bytes", type=int, default=8 * 1024 * 1024,
        help="reject a response body larger than this many bytes",
    )
    args = parser.parse_args()
    if args.port < 1 or args.port > 65535:
        parser.error("--port must be in 1..65535")
    if args.concurrency < 1 or args.requests_per_connection < 1:
        parser.error("--concurrency and --requests-per-connection must be positive")
    if args.warmup < 0 or args.timeout <= 0 or args.max_response_bytes < 1:
        parser.error("--warmup must be non-negative; timeout and max response size must be positive")
    return args


def make_payload(msg_id: int, query: str) -> bytes:
    # Separators avoid pretty-print whitespace; server-side JSON parsing is unchanged.
    return json.dumps({"msgID": msg_id, "msg": query}, ensure_ascii=False, separators=(",", ":")).encode("utf-8")


async def exchange(session: Session, payload: bytes, max_response_bytes: int) -> Response:
    started = time.perf_counter()
    session.writer.write(SIZE_T.pack(len(payload)) + payload)
    await session.writer.drain()

    header = await session.reader.readexactly(SIZE_T.size)
    body_size = SIZE_T.unpack(header)[0]
    if body_size > max_response_bytes:
        raise ValueError(f"response body is too large: {body_size} bytes")
    body = await session.reader.readexactly(body_size)

    response = json.loads(body.decode("utf-8"))
    response_msg_id = response.get("msgID")
    if not isinstance(response_msg_id, int):
        raise ValueError(f"response msgID is not an integer: {response_msg_id!r}")
    return Response(
        latency_ms=(time.perf_counter() - started) * 1000.0,
        msg_id=response_msg_id,
        pretty_json=json.dumps(response, ensure_ascii=False, indent=2),
    )


def is_expected_business_response(request_msg_id: int, response_msg_id: int) -> bool:
    return response_msg_id in {EXPECTED_SUCCESS_RESPONSE_IDS[request_msg_id], 404}


async def open_and_warmup(args: argparse.Namespace, payload: bytes) -> Session:
    reader, writer = await asyncio.wait_for(asyncio.open_connection(args.host, args.port), args.timeout)
    session = Session(reader, writer)
    try:
        for _ in range(args.warmup):
            response = await asyncio.wait_for(
                exchange(session, payload, args.max_response_bytes), args.timeout
            )
            if not is_expected_business_response(args.msg_id, response.msg_id):
                raise ValueError(f"unexpected response msgID: {response.msg_id}")
        return session
    except BaseException:
        writer.close()
        await writer.wait_closed()
        raise


async def measure_session(
    session: Session,
    args: argparse.Namespace,
    payload: bytes,
    gate: asyncio.Barrier,
    sample_recorder: SampleRecorder,
) -> WorkerResult:
    result = WorkerResult()
    try:
        await gate.wait()
        for request_index in range(args.requests_per_connection):
            try:
                response = await asyncio.wait_for(
                    exchange(session, payload, args.max_response_bytes), args.timeout
                )
                result.response_msg_ids[response.msg_id] = result.response_msg_ids.get(response.msg_id, 0) + 1
                if args.show_sample:
                    sample_recorder.record(response.pretty_json)

                if not is_expected_business_response(args.msg_id, response.msg_id):
                    result.abnormal_responses += 1
                    result.errors += args.requests_per_connection - request_index
                    result.samples.append(
                        f"request {request_index + 1}: unexpected response msgID {response.msg_id}"
                    )
                    break

                result.success += 1
                result.latencies_ms.append(response.latency_ms)
                if response.msg_id == EXPECTED_SUCCESS_RESPONSE_IDS[args.msg_id]:
                    result.business_success += 1
                else:
                    result.business_miss += 1
            except (asyncio.IncompleteReadError, asyncio.TimeoutError, OSError, UnicodeDecodeError, json.JSONDecodeError, ValueError) as exc:
                # A framing/connection failure makes the remaining pipelined sequence unreliable.
                result.errors += args.requests_per_connection - request_index
                result.samples.append(f"request {request_index + 1}: {type(exc).__name__}: {exc}")
                break
    finally:
        session.writer.close()
        await session.writer.wait_closed()
    return result


def percentile(sorted_values: Sequence[float], fraction: float) -> float:
    """Nearest-rank percentile; requires a non-empty ascending sequence."""
    return sorted_values[max(0, math.ceil(len(sorted_values) * fraction) - 1)]


def print_report(
    args: argparse.Namespace,
    elapsed_s: float,
    results: Sequence[WorkerResult],
    sample_recorder: SampleRecorder,
) -> None:
    total = args.concurrency * args.requests_per_connection
    success = sum(item.success for item in results)
    errors = total - success
    business_success = sum(item.business_success for item in results)
    business_miss = sum(item.business_miss for item in results)
    abnormal_responses = sum(item.abnormal_responses for item in results)
    response_msg_ids: dict[int, int] = {}
    for item in results:
        for msg_id, count in item.response_msg_ids.items():
            response_msg_ids[msg_id] = response_msg_ids.get(msg_id, 0) + count
    latencies = sorted(value for item in results for value in item.latencies_ms)

    print("SearchEngine benchmark")
    print(f"target: {args.host}:{args.port}")
    print(f"request: msgID={args.msg_id}, query={args.query!r}")
    print(f"concurrency: {args.concurrency}")
    print(f"requests per connection: {args.requests_per_connection}")
    print(f"warmup per connection: {args.warmup} (excluded from metrics)")
    print()
    print(f"total requests: {total}")
    print(f"success: {success}")
    print(f"protocol success: {success}")
    print(f"business success: {business_success}")
    print(f"business miss / 404: {business_miss}")
    print(f"abnormal responses: {abnormal_responses}")
    print("response msgID counts:")
    for msg_id, count in sorted(response_msg_ids.items()):
        print(f"  msgID={msg_id}: {count}")
    print(f"error rate: {errors / total * 100.0:.2f}% ({errors}/{total})")
    print(f"elapsed time: {elapsed_s:.3f} s")
    print(f"QPS: {success / elapsed_s:.2f}")
    if latencies:
        print(f"average latency: {statistics.fmean(latencies):.3f} ms")
        print(f"P50 latency: {percentile(latencies, 0.50):.3f} ms")
        print(f"P95 latency: {percentile(latencies, 0.95):.3f} ms")
        print(f"P99 latency: {percentile(latencies, 0.99):.3f} ms")
    else:
        print("average latency: N/A")
        print("P50 / P95 / P99 latency: N/A")

    samples = [sample for item in results for sample in item.samples]
    if samples:
        print("error samples:")
        for sample in samples[:5]:
            print(f"  - {sample}")
    if args.show_sample and sample_recorder.response is not None:
        print("sample response:")
        print(sample_recorder.response)


async def run(args: argparse.Namespace) -> int:
    payload = make_payload(args.msg_id, args.query)
    try:
        sessions = await asyncio.gather(
            *(open_and_warmup(args, payload) for _ in range(args.concurrency))
        )
    except (asyncio.TimeoutError, OSError, asyncio.IncompleteReadError, ValueError, json.JSONDecodeError) as exc:
        print(f"warmup/setup failed: {type(exc).__name__}: {exc}", file=sys.stderr)
        return 2

    gate = asyncio.Barrier(args.concurrency + 1)
    sample_recorder = SampleRecorder()
    workers = [
        asyncio.create_task(measure_session(session, args, payload, gate, sample_recorder))
        for session in sessions
    ]
    await gate.wait()
    started = time.perf_counter()
    results = await asyncio.gather(*workers)
    print_report(args, time.perf_counter() - started, results, sample_recorder)
    return 0 if all(result.errors == 0 for result in results) else 1


def main() -> int:
    return asyncio.run(run(parse_args()))


if __name__ == "__main__":
    raise SystemExit(main())
