# Task: semi-smart 'do nothing' function.  On the ground, shutdown
# motor and center controls.  If this tasks is initiated while we are
# flying, it means the mission manager ran out of things to do and has
# requested a circle hold over the current position.

from props import getNode

import comms.events
import mission.mission_mgr
from mission.task.task import Task

class Idle(Task):
    def __init__(self, config_node):
        Task.__init__(self)
        self.task_node = getNode("/task", True)
        self.ap_node = getNode("/autopilot", True)
        self.engine_node = getNode("/controls/engine", True)
        self.saved_fcs_mode = ""
        self.name = config_node.getString("name")
        self.nickname = config_node.getString("nickname")

    def activate(self):
        self.active = True

        # save existing state
        self.saved_fcs_mode = self.ap_node.getString("mode")

        # if not in the air, set a simple flight control mode that
        # does not touch the throttle, and set throttle to idle.
        if not self.task_node.getBool("is_airborne"):
            self.ap_node.setString("mode", "basic")
            self.engine_node.setFloat("throttle", 0.0)

    def update(self, dt):
        if not self.active:
            return False

        # If we are in the air, push a circle hold on the front of the
        # seq task queue
        if self.task_node.getBool("is_airborne"):
            # request a circle hold if we are in the air
            mission.mission_mgr.m.request_task_circle()

    def is_complete(self):
        return False

    def close(self):
        self.ap_node.setString("mode", self.saved_fcs_mode)
        self.active = False
        return True
