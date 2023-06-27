from __future__ import annotations
import rg2.bin._rg2
import typing
import numpy

__all__ = ["WalkerEnv"]

class WalkerEnv:
    def __init__(self, resourceDir: str, visualizable: bool) -> None: ...
    def close(self) -> None: ...
    def curriculumUpdate(self) -> None: ...
    def getActionDim(self) -> int: ...
    def getObDim(self) -> int: ...
    def init(self) -> None: ...
    def isTerminalState(self, arg0: float) -> bool: ...
    def observe(self, arg0: numpy.ndarray) -> None: ...
    def reset(self) -> None: ...
    def setControlTimeStep(self, arg0: float) -> None: ...
    def setSeed(self, arg0: int) -> None: ...
    def setSimulationTimeStep(self, arg0: float) -> None: ...
    def startRecordingVideo(self, arg0: str) -> None: ...
    def step(self, arg0: numpy.ndarray) -> float: ...
    def stopRecordingVideo(self) -> None: ...
    def turnOffVisualization(self) -> None: ...
    def turnOnVisualization(self) -> None: ...
    pass
