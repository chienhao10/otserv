using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.IO;
using System.Reflection;
using AutoBuddy.Humanizers;
using AutoBuddy.MainLogics;
using AutoBuddy.MyChampLogic;
using AutoBuddy.Utilities;
using AutoBuddy.Utilities.AutoLvl;
using AutoBuddy.Utilities.AutoShop;
using EloBuddy;
using EloBuddy.SDK;
using EloBuddy.SDK.Events;
using EloBuddy.SDK.Menu;
using EloBuddy.SDK.Menu.Values;
using SharpDX;
using Version = System.Version;

namespace AutoBuddy
{
    internal static class Program
    {
        private static Menu menu;
        private static IChampLogic myChamp;
        private static LogicSelector Logic { get; set; }
        static List<string> _allowed = new List<string> { "/noff", "/ff", "/mute all", "/msg", "/r", "/w", "/surrender", "/nosurrender", "/help", "/dance", "/d", "/taunt", "/t", "/joke", "/j", "/laugh", "/l" };

        private static void Main(string[] args)
        {
            Loading.OnLoadingComplete += Loading_OnLoadingComplete;
        }

        private static void Loading_OnLoadingComplete(EventArgs args)
        {
            /*
                  foreach (Obj_AI_Base so in ObjectManager.Get<Obj_AI_Turret>().Where(so => so.Team == ObjectManager.Player.Team))
                  {
                      Chat.Print(so.Name);
                  }


            //LANES
            //Shrine_A

            //C_01_A = torre do nexus
            //L_02_A = T2 do top
            //L_03_A = t1 do top
            //C_06_A = t3 do top

            //R_02_A = t2 do bot
            //R_03_A = t1 do bot
            //C_07_A = t3 do bot

            foreach (Obj_AI_Base so in ObjectManager.Get<Obj_AI_Turret>().Where(so => so.Name.EndsWith("R_03_A") && so.Team == ObjectManager.Player.Team))
            {
                Player.IssueOrder(GameObjectOrder.MoveTo, so.Position);
            }
            */
            createFS();
            Chat.Print("Welcome to Auto Buddy Treeline.");
            Core.DelayAction(Start, 3000);
            menu = MainMenu.AddMenu("AB Treeline", "AB");
            menu.AddGroupLabel("Default");
            CheckBox c =
                new CheckBox("Call bot, will leave if other player stays on bot (only auto lane)", true);

            PropertyInfo property2 = typeof(CheckBox).GetProperty("Size");
            property2.GetSetMethod(true).Invoke(c, new object[] { new Vector2(500, 20) });
            menu.Add("bot", c);

            Slider sliderLanes = menu.Add("lane", new Slider(" ", 1, 1, 2));
            string[] lanes =
            {
                "", "Selected lane: Top", "Selected lane: Bot"
            };
            sliderLanes.DisplayName = lanes[sliderLanes.CurrentValue];
            sliderLanes.OnValueChange += delegate(ValueBase<int> sender, ValueBase<int>.ValueChangeArgs changeArgs)
            {
                sender.DisplayName = lanes[changeArgs.NewValue];
            };

            menu.Add("disablepings", new CheckBox("Disable pings", false));
            menu.Add("disablechat", new CheckBox("Disable chat", false));

            menu.AddSeparator(5);
            menu.AddGroupLabel("Surrender");
            Slider sliderFF = menu.Add("ff", new Slider(" ", 2, 1, 2));
            string[] ffStrings =
            {
                "", "Surrender Vote: Yes", "Surrender Vote: No"
            };
            sliderFF.DisplayName = ffStrings[sliderFF.CurrentValue];
            sliderFF.OnValueChange += delegate(ValueBase<int> sender, ValueBase<int>.ValueChangeArgs changeArgs)
            {
                sender.DisplayName = ffStrings[changeArgs.NewValue];
            };

            menu.AddSeparator(5);
            menu.AddGroupLabel("Others");
            menu.Add("lockchat", new CheckBox("Lock my chat", true));
            CheckBox newpf = new CheckBox("Use smart pathfinder", true);
            menu.Add("newPF", newpf);
            newpf.OnValueChange += newpf_OnValueChange;
            menu.Add("reselectlane", new CheckBox("Reselect lane", false));

            menu.AddLabel("----------------------------");
            menu.Add("autoclose", new CheckBox("Auto close lol. Need Reload (F5)", true));
            menu.Add("oldWalk", new CheckBox("Use old orbwalk. Need Reload (F5)", false));
            menu.Add("debuginfo", new CheckBox("Draw debug info", false));
            menu.Add("l1", new Label("By Christian Brutal Sniper - Updated and fixed by Tryller"));

            Chat.OnInput += Chat_OnInput;
        }


        private static void Chat_OnInput(ChatInputEventArgs args)
        {
            if (MainMenu.GetMenu("AB").Get<CheckBox>("lockchat").CurrentValue)
            {
                args.Process = false;
                if (_allowed.Any(str => args.Input.StartsWith(str)))
                    args.Process = true;
            }
            else 
            {
                args.Process = true;
            }
        }

        static void newpf_OnValueChange(ValueBase<bool> sender, ValueBase<bool>.ValueChangeArgs args)
        {
            AutoWalker.newPF = args.NewValue;
        }

        private static void Start()
        {
            RandGen.Start();
            bool generic = false;
            switch (ObjectManager.Player.Hero)
            {
                case Champion.Sivir:
                    myChamp = new Sivir();
                    break;
                case Champion.Ashe:
                    myChamp = new Ashe();
                    break;
                case Champion.Caitlyn:
                    myChamp = new Caitlyn();
                    break;
                case Champion.Ezreal:
                    myChamp = new Ezreal();
                    break;
                case Champion.Jinx:
                    myChamp = new Jinx();
                    break;
                case Champion.Cassiopeia:
                    myChamp = new Cassiopeia();
                    break;
                case Champion.Vayne:
                    myChamp = new Vayne();
                    break;
                case Champion.Tristana:
                    myChamp = new Tristana();
                    break;
                default:
                    generic = true;
                    myChamp = new Generic();
                    break;
            }
            CustomLvlSeq cl = new CustomLvlSeq(menu, AutoWalker.myHero, Path.Combine(Environment.GetFolderPath(
            Environment.SpecialFolder.ApplicationData), "EloBuddy\\AutoBuddyTreeline\\Skills"));
            if (!generic)
            {
                BuildCreator bc = new BuildCreator(menu, Path.Combine(Environment.GetFolderPath(
                    Environment.SpecialFolder.ApplicationData), "EloBuddy\\AutoBuddyTreeline\\Builds"), myChamp.ShopSequence);
            }


            else
            {
                myChamp = new Generic();
                if (MainMenu.GetMenu("AB_" + ObjectManager.Player.ChampionName) != null &&
                    MainMenu.GetMenu("AB_" + ObjectManager.Player.ChampionName).Get<Label>("shopSequence") != null)
                {
                    Chat.Print("Auto Buddy Plus: Loaded shop plugin for " + ObjectManager.Player.ChampionName);
                    BuildCreator bc = new BuildCreator(menu, Path.Combine(Environment.GetFolderPath(
                        Environment.SpecialFolder.ApplicationData), "EloBuddy\\AutoBuddyTreeline\\Builds"),
                        MainMenu.GetMenu("AB_" + ObjectManager.Player.ChampionName)
                            .Get<Label>("shopSequence")
                            .DisplayName);
                }
                else
                {
                    BuildCreator bc = new BuildCreator(menu, Path.Combine(Environment.GetFolderPath(
                        Environment.SpecialFolder.ApplicationData), "EloBuddy\\AutoBuddyTreeline\\Builds"), myChamp.ShopSequence);
                }
            }

            Logic = new LogicSelector(myChamp, menu);
        }

        private static void createFS()
        {
            Directory.CreateDirectory(Path.Combine(Environment.GetFolderPath(
                Environment.SpecialFolder.ApplicationData), "EloBuddy\\AutoBuddyTreeline"));
            Directory.CreateDirectory(Path.Combine(Environment.GetFolderPath(
                Environment.SpecialFolder.ApplicationData), "EloBuddy\\AutoBuddyTreeline\\Builds"));
            Directory.CreateDirectory(Path.Combine(Environment.GetFolderPath(
                Environment.SpecialFolder.ApplicationData), "EloBuddy\\AutoBuddyTreeline\\Skills"));
        }
    }
}