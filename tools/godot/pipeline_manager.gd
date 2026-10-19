# Replaces v2.2's scripts/PipelineManager.gd in the unpacked copy (never in
# the player's game). The game's version asks Godot for a rendering device and,
# finding none when headless, takes its OpenGL path: it hides the table,
# backroom and restroom for 0.1 s, hides more nodes and halves the lights. The
# exports must show what the Vulkan renderer draws, so this one keeps using_gl
# false and leaves the scene alone. Same class name and exports as the game's.
class_name PipelineManager extends Node

@export var env : WorldEnvironment
@export var cam : Node
@export var scene : String
@export var overlay : Control
@export var main_array_lights : Array[Light3D]
@export var main_array_hide : Array[Node3D]
@export var main_array_toggle : Array[Node3D]
@export var main_restroom : Node3D
@export var main_light_tabletop2 : Light3D
@export var main_light_tabletop : Light3D

func _ready():
	CheckStatus()

func CheckStatus():
	GlobalVariables.using_gl = false

func Toggle():
	pass

func AdjustSettings(_scene : String):
	pass
