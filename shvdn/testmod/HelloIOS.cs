// Test mod for the iOS port's ScriptHook 2: proves the whole chain (runtime -> ScriptHookV .NET -> natives -> game).
// Shows a notification once, then a line of on-screen text with live values read from the game every frame.
using System;
using GTA;
using GTA.UI;

public class HelloIOS : Script
{
    int _frames;
    bool _greeted;

    public HelloIOS()
    {
        Tick += OnTick;
    }

    void OnTick(object sender, EventArgs e)
    {
        _frames++;
        if (!_greeted && _frames > 30)
        {
            _greeted = true;
            Notification.Show("~g~ScriptHook 2~s~: .NET mods work on iOS!");
        }
        Ped me = Game.Player.Character;
        string text = $".NET mod running - health {me.Health}, speed {me.Speed:0.0} m/s, frame {_frames}";
        new TextElement(text, new System.Drawing.PointF(20f, 20f), 0.35f).Draw();
    }
}
