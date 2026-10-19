# Replaces the GlobalSteam autoload (scripts/Steam.gd) in every Godot run our
# tools make. The real one calls Steam.steamInit with the game's app id, so a
# run that unlocks an achievement or opens the leaderboard would reach the
# player's Steam account. This one never starts Steam.
#
# It keeps the variables v2.2's scripts read (MenuManager reads ONLINE and
# LOBBY_INVITE_ARG), with the values Steam.gd has before it connects.
extends Node

const PACKET_READ_LIMIT: int = 32

var DATA
var LOBBY_ID = 0
var LOBBY_MEMBERS = []
var USER_ID_LIST_TO_IGNORE : Array[int]
var LOBBY_INVITE_ARG = false
var STEAM_ID = 0
var STEAM_NAME = ""
var ONLINE = false
var HOST_ID = 0

var is_lobby_friends_only = true
var lobby_player_limit = 4

func _ready():
	print("steam: not initialised for this run (tools/godot/steam_off.gd)")
