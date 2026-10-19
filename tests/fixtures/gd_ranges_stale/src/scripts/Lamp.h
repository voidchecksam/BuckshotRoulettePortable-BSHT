// fix_gd_ranges.py --check must refuse this: TurnOff is L8-L9 in Lamp.gd.
class Lamp {
public:
    bool lit = false;   // gd: L3
    void TurnOn();      // gd: L5-L6
    void TurnOff();     // gd: L7-L8
};
