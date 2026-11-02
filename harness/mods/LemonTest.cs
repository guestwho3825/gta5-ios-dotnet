// Test mod for the lab: a LemonUI menu (the menu library most newer .NET mods use), opened with F9.
using System;
using System.Windows.Forms;
using GTA;
using LemonUI;
using LemonUI.Menus;

public class LemonTest : Script
{
    readonly ObjectPool _pool = new ObjectPool();
    readonly NativeMenu _menu = new NativeMenu("Lemon Test", "LemonUI on iOS");

    public LemonTest()
    {
        var item = new NativeItem("Say hello", "Shows a notification");
        item.Activated += (s, e) => GTA.UI.Notification.Show("LemonUI works");
        _menu.Add(item);
        _menu.Add(new NativeCheckboxItem("A checkbox", true));
        _menu.Add(new NativeSliderItem("A slider", 10, 5));
        var sub = new NativeMenu("Submenu", "Second level");
        sub.Add(new NativeItem("Inside the submenu"));
        _menu.AddSubMenu(sub);
        _pool.Add(_menu); _pool.Add(sub);
        _menu.Shown += (s, e) => GTA.UI.Notification.Show("LemonUI menu opened");
        Tick += (s, e) => _pool.Process();
        KeyDown += (s, e) => { if (e.KeyCode == Keys.F9) _menu.Visible = !_menu.Visible; };
    }
}
