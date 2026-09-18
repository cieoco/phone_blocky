"""Run with .venv-check/Scripts/python -m unittest discover -s tests -v."""
import json
import re
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "ai-relay"))
from schema import ProgProgram
from prompts import SYSTEM_PROMPT
from pydantic import ValidationError
from unittest.mock import patch
from types import SimpleNamespace


def program(*commands):
    return {"mode": "PROG", "setup": list(commands), "loop": []}


def branch(left=1, right=2):
    return {"command": "if", "condition": {"command": "logic_compare", "operator": "LT", "left": left, "right": right}, "then": [{"cmd": "stop"}]}


class AlignmentTests(unittest.TestCase):
    def test_browser_relay_contract(self):
        good = [program(), program({"cmd": "pwm", "motor": 1, "duty": -100}),
                program({"cmd": "servo", "ch": 2, "deg": 180}), program({"cmd": "delay", "ms": 10000}),
                program({"cmd": "move_to", "motor": 3, "deg": 9000}),
                program({"cmd": "move_by", "motor": 4, "deg": -4525}),
                program({"cmd": "zero", "motor": 4}),
                program(branch({"command": "legoButton", "pin": "4"})),
                program(branch({"command": "arduinoUltrasonic", "trigPin": 2, "echoPin": "33"})),
                program(*[{"cmd": "stop"} for _ in range(64)])]
        good += [program({"cmd": "speed", "motor": 3, "rpm": rpm}) for rpm in [0, 60, 250]]
        good.append(program({"cmd":"pwm", "motor":1.0, "duty":50.0}))
        bad = [{"mode": "PROG"}, {"mode": "PROG", "setup": [], "loop": None},
               program({"cmd": "pwm", "motor": True, "duty": 40}),
               program({"cmd": "pwm", "motor": "1", "duty": 40}),
               program({"cmd": "pwm", "motor": 1, "duty": 101}),
               program({"cmd": "servo", "ch": 3, "deg": 90}),
               program({"cmd": "delay", "ms": -1}), program({"cmd": "delay", "ms": 10001}),
               program({"cmd": "move_to", "motor": 1, "deg": 9000}),
               program({"cmd": "move_by", "motor": 4, "deg": 2147483648}),
               program({"cmd": "speed", "motor": 3, "rpm": 59}),
               program({"cmd": "zero", "motor": 2}), program(branch(True)),
               program({"cmd": "unknown", **branch()}),
               program({"command": "if", "condition": branch()["condition"]}),
               program(*[{"cmd": "stop"} for _ in range(65)])]
        for pin in ["2junk", "", " 2", "2.5", 40, -1, True, "２"]:
            bad.append(program(branch({"command": "arduinoUltrasonic", "trigPin": pin, "echoPin": 33})))
            bad.append(program(branch({"command": "legoButton", "pin": pin})))
        for cmd in ["while", "until", "repeat", "unknown"]:
            bad.append(program({"command": cmd, "condition": branch()["condition"], "do": []}))
        nested = branch(); nested["then"] = [{"cmd": "stop"} for _ in range(64)]
        bad.append(program(nested))
        cases = good + bad
        expected = [True] * len(good) + [False] * len(bad)
        actual = []
        for case in cases:
            try:
                actual.append(ProgProgram.model_validate(case).total_commands() <= 64)
            except ValidationError:
                actual.append(False)
        self.assertEqual(actual, expected)
        result = subprocess.run(["node", "tests/check_frontend.cjs", "--validate"], cwd=ROOT,
                                input=json.dumps(cases), text=True, capture_output=True, check=True)
        self.assertEqual(json.loads(result.stdout), expected)

    def test_relay_handlers_without_network(self):
        import main
        good = program({"cmd": "move_to", "motor": 3, "deg": 9000})
        self.assertTrue(main.validate(good).ok)
        self.assertFalse(main.validate({"mode": "PROG"}).ok)
        nested = branch(); nested['then'] = [{'cmd':'stop'}] * 64
        self.assertFalse(main.validate(program(nested)).ok)
        for payload, ok in [(json.dumps(good), True), ('not json', False), (json.dumps(program(nested)), False)]:
            response = SimpleNamespace(choices=[SimpleNamespace(message=SimpleNamespace(content=payload))])
            with patch.object(main, 'get_client') as client:
                client.return_value.chat.completions.create.return_value = response
                self.assertEqual(main.generate(main.GenerateRequest(prompt='offline fixture')).ok, ok)

    def test_prompts_and_examples(self):
        html = (ROOT / "data/ai.html").read_text(encoding="utf-8")
        embedded = re.search(r'const SYSTEM_PROMPT = (.*);', html).group(1)
        self.assertEqual(json.loads(embedded), SYSTEM_PROMPT)
        examples = [json.loads(line) for line in SYSTEM_PROMPT.splitlines() if line.startswith('{"mode"')]
        self.assertGreaterEqual(len(examples), 5)
        for example in examples:
            ProgProgram.model_validate(example)
        self.assertEqual(examples[0]["loop"], [])
        self.assertEqual(examples[2]["loop"], [])

    def test_hardware_metadata(self):
        config = (ROOT / "include/config.h").read_text(encoding="utf-8")
        constants = dict(re.findall(r'static const int (\w+) = (\d+);', config))
        hw = json.loads((ROOT / "data/hw_config.json").read_text())
        for servo in hw['servos']:
            self.assertEqual(servo['pin'], int(constants[f'SERVO{servo["id"]}_PIN']))
        self.assertEqual([m['has_encoder'] for m in hw['motors']], [False,False,True,True])
        for motor in hw['motors'][2:]:
            for phase in ['A','B']:
                self.assertEqual(motor['enc_'+phase.lower()], int(constants[f'ENCODER_M{motor["id"]}_PIN_{phase}']))
        for key, suffix in {'lego_button':'LEGO_BUTTON','ultrasonic_trig':'ULTRASONIC_TRIG','ultrasonic_echo':'ULTRASONIC_ECHO','digital_input':'DIGITAL_INPUT','analog_input':'ANALOG_INPUT'}.items():
            self.assertEqual(hw['user_pins'][key], int(constants[f'USER_{suffix}_PIN']))
        motor_header = (ROOT/'lib/AFMotor/src/AFMotor.h').read_text(encoding='utf-8')
        channels = [int(x) for x in re.findall(r'#define PWM\d_CHANNEL (\d+)', motor_header)]
        self.assertEqual(hw['pwm']['motor_channels'], channels)
        timer = lambda ch: (ch // 2) % 4
        self.assertTrue(set(map(timer, channels)).isdisjoint(map(timer, hw['pwm']['general_channels'])))
        self.assertNotIn(hw['pwm']['servo_timer'], set(map(timer, channels + hw['pwm']['general_channels'])))


if __name__ == '__main__':
    unittest.main()
