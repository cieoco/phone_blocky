"""PROG JSON schema validation.

對齊 docs/prog_json_schema.md。AI 輸出與 ai.html 傳回的 JSON 都會經過這裡。

if/sensor 區塊沿用 Blockly 既有形狀 (`command:` key, logic_compare 巢狀
條件),這樣裝置端與 Blockly 共用同一個 parser。其他指令 (pwm/stop/servo/
delay) 沿用 Phase A 的精簡 `cmd:` 格式。
"""
from __future__ import annotations

from typing import List, Literal, Union

from pydantic import BaseModel, ConfigDict, Field, conint


# --- 精簡動作指令 (AI 主力) ---

class CmdPwm(BaseModel):
    cmd: Literal["pwm"]
    motor: conint(ge=1, le=4)
    duty: conint(ge=-100, le=100)


class CmdStop(BaseModel):
    cmd: Literal["stop"]
    motor: conint(ge=0, le=4) = 0


class CmdServo(BaseModel):
    cmd: Literal["servo"]
    ch: conint(ge=1, le=2)
    deg: conint(ge=0, le=180)


class CmdDelay(BaseModel):
    cmd: Literal["delay"]
    ms: conint(ge=0, le=10000)


# --- Blockly 既有形狀:感測器讀值 ---
# trigPin/echoPin 用字串對齊 Blockly IR 預設

class ReadUltrasonic(BaseModel):
    command: Literal["arduinoUltrasonic"]
    trigPin: Union[int, str]
    echoPin: Union[int, str]


# --- Blockly 既有形狀:比較 ---
# operator 用 Blockly 字串 (LT/GT/EQ/NEQ/LTE/GTE)
# left / right 可以是會回值的感測器指令,或整數常數

ConditionOperand = Union[ReadUltrasonic, int]


class LogicCompare(BaseModel):
    command: Literal["logic_compare"]
    operator: Literal["LT", "GT", "EQ", "NEQ", "LTE", "GTE"]
    left: ConditionOperand
    right: ConditionOperand


# --- Blockly 既有形狀:if ---

class CmdIf(BaseModel):
    model_config = ConfigDict(populate_by_name=True)

    command: Literal["if"]
    condition: LogicCompare
    then_: List["Command"] = Field(default_factory=list, alias="then")
    else_: List["Command"] = Field(default_factory=list, alias="else")


Command = Union[CmdPwm, CmdStop, CmdServo, CmdDelay, CmdIf]

CmdIf.model_rebuild()


class ProgProgram(BaseModel):
    mode: Literal["PROG"]
    setup: List[Command] = Field(default_factory=list)
    loop: List[Command] = Field(default_factory=list)

    def total_commands(self) -> int:
        return _count(self.setup) + _count(self.loop)


def _count(cmds: List[Command]) -> int:
    total = 0
    for c in cmds:
        total += 1
        if isinstance(c, CmdIf):
            total += _count(c.then_) + _count(c.else_)
    return total
