function init_with_data(self, rect, stream)

    self.states = {}
    self.states.name = "slam_door"

    self.states.Closed =
    {
        animation = "SLAM.BAN_2020_AePc_1",
        tick = function(s, i)
            -- TODO: Detect if activated switch to opening
        end
    }

    self:ScriptLoadAnimations()

    self.states.Active = self.states.Closed
    self:SetAnimation(self.states.Active.animation)
end
