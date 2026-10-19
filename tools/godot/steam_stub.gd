# Stands in for the GodotSteam singleton the shipped game is built with. The
# single-player scripts call these; every one does nothing. The lobby signals
# are here because the v2.2 menu's lobby manager connects them in _ready.
extends Node

signal leaderboard_find_result(handle, found)
signal leaderboard_score_uploaded(success, handle, score)
signal leaderboard_scores_downloaded(message, handle, result)
signal avatar_loaded(user_id, size, buffer)
signal join_requested(lobby_id, steam_id)
signal lobby_chat_update(lobby_id, change_id, making_change_id, chat_state)
signal lobby_created(connect, lobby_id)
signal lobby_joined(lobby_id, permissions, locked, response)
signal lobby_match_list(lobbies)
signal persona_state_change(steam_id, flags)

const LEADERBOARD_DATA_REQUEST_GLOBAL := 0
const LEADERBOARD_DATA_REQUEST_GLOBAL_AROUND_USER := 1
const LEADERBOARD_DATA_REQUEST_FRIENDS := 2

func steamInit(_retrieve_stats := false) -> Dictionary: return {"status": 0, "verbal": "stub"}
func run_callbacks() -> void: pass
func loggedOn() -> bool: return false
func getSteamID() -> int: return 0
func getPersonaName() -> String: return ""
func getFriendPersonaName(_id = 0) -> String: return ""
func setAchievement(_name := "") -> bool: return false
func clearAchievement(_name := "") -> bool: return false
func storeStats() -> bool: return false
func findLeaderboard(_name := "") -> void: pass
func setLeaderboardDetailsMax(_max := 0) -> int: return 0
func getLeaderboardEntryCount(_handle = 0) -> int: return 0
func downloadLeaderboardEntries(_start := 0, _end := 0, _type := 0, _handle = 0) -> void: pass
func uploadLeaderboardScore(_score = 0, _keep_best := true, _details = [], _handle = 0) -> void: pass
