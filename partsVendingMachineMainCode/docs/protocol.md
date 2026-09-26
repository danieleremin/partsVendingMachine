# Command Protocol

A line-based ASCII protocol for requesting dispenses. It is the same whether
the lines travel over stdin/stdout (Python simulation), USB serial at
`SERIAL_BAUD` (firmware), or a future Raspberry Pi / web bridge.

## Framing

- One command per line, terminated by `\n`. A preceding `\r` is ignored, so
  `\r\n` works too (Arduino Serial Monitor, Windows terminals).
- Leading/trailing whitespace is ignored. Fields are separated by one or more
  spaces or tabs.
- Command letters are case-insensitive (`d 3` == `D 3`). Replies always use
  upper case.
- Lines longer than `CMD_MAX_LINE_LEN` characters (excluding the terminator)
  are discarded and answered with `ERR LINE_TOO_LONG`.
- Empty lines are ignored with no reply.

## Commands (host → machine)

| Command | Meaning |
|---|---|
| `D <bin>` | Dispense one part from `<bin>`. `<bin>` is a decimal integer, 0-based, in `0 .. BIN_COUNT-1`. |
| `P` | Ping. Replies `OK P`. Lets a host check the link is alive. |

Examples: `D 0`, `d 11`, `P`.

## Replies (machine → host)

Every command line gets exactly one **final reply**, which starts with `OK`,
`FAIL`, or `ERR`. Before the final reply the machine may send any number of
**`STATE` lines**. A host that only cares about outcomes can skip every line
that doesn't start with `OK`, `FAIL`, or `ERR`.

| Reply | Meaning |
|---|---|
| `STATE <NAME>` | Entered state `<NAME>` from [state-machine.md](state-machine.md). Informational. |
| `OK D <bin> <attempts>` | Part detected. `<attempts>` is the number of gate openings used, `1 .. MAX_ATTEMPTS`. |
| `FAIL D <bin> <attempts>` | No part detected after all attempts. `<attempts>` is always `MAX_ATTEMPTS`. |
| `OK P` | Reply to ping. |
| `ERR <CODE> [detail]` | Command rejected. The machine did not move. |

### Error codes

| Code | When |
|---|---|
| `ERR UNKNOWN_CMD <text>` | First field isn't a known command letter. |
| `ERR BAD_ARG <text>` | Wrong number of fields, or `<bin>` isn't a plain decimal integer (e.g. `D`, `D x`, `D 1 2`, `D -1`, `D 1.5`). |
| `ERR BIN_RANGE <bin>` | `<bin>` parsed but is `>= BIN_COUNT`. |
| `ERR LINE_TOO_LONG` | Line exceeded `CMD_MAX_LINE_LEN`. |
| `ERR BUSY` | A command arrived while a dispense was in progress. It is dropped, not queued. |

`D -1` is `BAD_ARG` rather than `BIN_RANGE` because a leading `-` isn't a valid
bin token; `BIN_RANGE` is only for well-formed numbers that are too large.

## Example session

With `MAX_ATTEMPTS = 4`, `>` = sent by host, `<` = sent by machine:

```
> D 3
< STATE ROTATING
< STATE GATE_OPENING
< STATE WAITING_FOR_SENSOR
< STATE GATE_CLOSING
< STATE RETRY
< STATE GATE_OPENING
< STATE WAITING_FOR_SENSOR
< STATE GATE_CLOSING
< STATE SUCCESS
< STATE IDLE
< OK D 3 2
> D 12
< ERR BIN_RANGE 12
> X
< ERR UNKNOWN_CMD X
> P
< OK P
```

`STATE IDLE` is sent before the final reply, so the final reply is always the
last line of a command's output.
