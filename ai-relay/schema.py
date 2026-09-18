"""PROG JSON schema validation.

對齊 docs/prog_json_schema.md。AI 輸出與 ai.html 傳回的 JSON 都會經過這裡。

if/sensor 區塊沿用 Blockly 既有形狀 (`command:` key, logic_compare 巢狀
條件),這樣裝置端與 Blockly 共用同一個 parser。動作指令沿用精簡 `cmd:` 格式，包含 PWM、停止、舵機、延遲、定位、速度與歸零。
"""
from __future__ import annotations

from typing import List, Literal, Union

from pydantic import BaseModel, ConfigDict, Field, StrictInt, conint, field_validator, model_validator


class StrictModel(BaseModel):
    model_config = ConfigDict(strict=True)

    @field_validator("*", mode="before")
    @classmethod
    def integral_json_number(cls, value):
        # JSON 1 and 1.0 are both integer-valued numbers in JavaScript.
        if isinstance(value, float) and value.is_integer():
            return int(value)
        return value

    @model_validator(mode="before")
    @classmethod
    def unambiguous_command(cls, value):
        if isinstance(value, dict) and "cmd" in value and "command" in value:
            raise ValueError("Use only one of cmd / command")
        return value


# --- 精簡動作指令 (AI 主力) ---

class CmdPwm(StrictModel):
    cmd: Literal["pwm"]
    motor: conint(ge=1, le=4)
    duty: conint(ge=-100, le=100)


class CmdStop(StrictModel):
    cmd: Literal["stop"]
    motor: conint(ge=0, le=4) = 0


class CmdServo(StrictModel):
    cmd: Literal["servo"]
    ch: conint(ge=1, le=2)
    deg: conint(ge=0, le=180)


class CmdDelay(StrictModel):
    cmd: Literal["delay"]
    ms: conint(ge=0, le=10000)


# --- Blockly 既有形狀:感測器讀值 ---
# trigPin/echoPin 用字串對齊 Blockly IR 預設

class ReadUltrasonic(StrictModel):
    command: Literal["arduinoUltrasonic"]
    trigPin: Union[int, str]
    echoPin: Union[int, str]

    @field_validator("trigPin", "echoPin")
    @classmethod
    def valid_pin(cls, value):
        if isinstance(value, str) and (not value.isascii() or not value.isdecimal()):
            raise ValueError("GPIO must be an integer or decimal string in 0..39")
        if not 0 <= int(value) <= 39:
            raise ValueError("GPIO must be in 0..39")
        return value


class ReadLegoButton(StrictModel):
    command: Literal["legoButton"]
    pin: Union[int, str]
    _valid_pin = field_validator("pin")(ReadUltrasonic.valid_pin.__func__)


class CmdMove(StrictModel):
    cmd: Literal["move_to", "move_by"]
    motor: conint(ge=3, le=4)
    deg: conint(ge=-2147483648, le=2147483647)


class CmdZero(StrictModel):
    cmd: Literal["zero"]
    motor: conint(ge=3, le=4)


class CmdSpeed(StrictModel):
    cmd: Literal["speed"]
    motor: conint(ge=3, le=4)
    rpm: conint(ge=0, le=250)

    @field_validator("rpm")
    @classmethod
    def valid_rpm(cls, value):
        if value != 0 and value < 60:
            raise ValueError("rpm must be 0 or 60..250")
        return value


# --- Blockly 既有形狀:比較 ---
# operator 用 Blockly 字串 (LT/GT/EQ/NEQ/LTE/GTE)
# left / right 可以是會回值的感測器指令,或整數常數

ConditionOperand = Union[ReadUltrasonic, ReadLegoButton, StrictInt]


class LogicCompare(StrictModel):
    command: Literal["logic_compare"]
    operator: Literal["LT", "GT", "EQ", "NEQ", "LTE", "GTE"]
    left: ConditionOperand
    right: ConditionOperand


# --- Blockly 既有形狀:if ---

class CmdIf(StrictModel):
    command: Literal["if"]
    condition: LogicCompare
    then_: List["Command"] = Field(alias="then")
    else_: List["Command"] = Field(default_factory=list, alias="else")


Command = Union[CmdPwm, CmdStop, CmdServo, CmdDelay, CmdMove, CmdZero, CmdSpeed, CmdIf]

CmdIf.model_rebuild()


class ProgProgram(StrictModel):
    mode: Literal["PROG"]
    setup: List[Command]
    loop: List[Command]

    def total_commands(self) -> int:
        return _count(self.setup) + _count(self.loop)


def _count(cmds: List[Command]) -> int:
    total = 0
    for c in cmds:
        total += 1
        if isinstance(c, CmdIf):
            total += _count(c.then_) + _count(c.else_)
    return total
