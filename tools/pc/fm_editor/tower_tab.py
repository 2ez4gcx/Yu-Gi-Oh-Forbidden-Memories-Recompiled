"""The Tower tab: a mod's "tower" (tower.py, notes/tower-mode.md), the run of
duels the title's second menu offers with the mod on. The tower's name, its
menu button's words, when it opens, what a loss does and its story on the
left, with the mod's duelists that only the tower lets out; its floors on
the right, each with its opponent, field, LP, rules, deck, reward and lines.
Written to the manifest as "tower", and "tower_only" on the duelists."""
from __future__ import annotations

import tkinter as tk
from tkinter import ttk

from . import duelist_rules, roster, tower
from .tabs import Tab
from .widgets import ScrolledForm

STORY_HINT = ("A line a box. @Name: said by a duelist, #N: a campaign portrait (0-24), #N @Name: both, "
              "@none: nobody's. \\n breaks a line inside the box; a backslash keeps the next letter as it is "
              "(\\@ for words that start with @).")


class TowerTab(Tab):
    def __init__(self, notebook, app):
        super().__init__(notebook, app, "Tower")
        self.scroll = ScrolledForm(self)
        self.scroll.pack(fill="both", expand=True)
        body = self.scroll.body
        self.has = tk.BooleanVar()
        ttk.Checkbutton(body, text="This mod has a tower (a button in the title's second menu)", variable=self.has,
                        command=self.toggle).pack(anchor="w")
        ttk.Label(body, style="Hint.TLabel", text="Each floor is a duel; a save keeps how far it climbed. "
                                                  "notes/tower-mode.md says the rest.").pack(anchor="w")
        self.status = ttk.Label(body, style="Error.TLabel", wraplength=1100, justify="left")
        self.status.pack(anchor="w", pady=(2, 4))
        columns = ttk.Frame(body)
        columns.pack(fill="both", expand=True)
        left = ttk.Frame(columns)
        left.grid(row=0, column=0, sticky="nw", padx=(0, 10))
        right = ttk.Frame(columns)
        right.grid(row=0, column=1, sticky="nw")
        self.vars = {}
        self.texts = {}
        self._tower_form(left)
        self._floors_form(right)
        self.floors = []          # the floors as the form edits them
        self.current = None       # the floor in the form
        self.kept = {}            # the tower's keys the form does not show, as written
        self.written = None       # the tower as the project had it when the form was filled
        self.shown = None         # (floor, form) as _show_floor filled it
        self.top_shown = None     # the tower's own form as refresh filled it

    # The fields of a floor and of the tower itself, in the form.  A form
    # nobody touched writes nothing back: the mod keeps its own spelling
    # ("Yami", {"text": ...} lines, key order) instead of the tab's.
    FLOOR_FIELDS = ("duelist", "field", "lp.player", "lp.opponent", "no_fusions", "no_equips", "checkpoint",
                    "reward.cards", "reward.starchips", "reward.unlock_duelist")
    FLOOR_TEXTS = ("deck", "before", "after")
    TOP_FIELDS = ("name", "label", "music", "unlock", "unlock.beat", "unlock.wins", "unlock.story", "unlock.card",
                  "unlock.copies", "unlock.after_ending", "on_lose")
    TOP_TEXTS = ("intro", "ending", "lose")

    def _form(self, fields, texts) -> tuple:
        return tuple(self.vars[k].get() for k in fields) + tuple(self._get_text(k) for k in texts)

    # --- the form -------------------------------------------------------------------

    def _entry(self, parent, row, key, label, width=24, hint=""):
        ttk.Label(parent, text=label).grid(row=row, column=0, sticky="w")
        var = self.vars[key] = tk.StringVar()
        entry = ttk.Entry(parent, textvariable=var, width=width)
        entry.grid(row=row, column=1, sticky="w", padx=(4, 0), pady=1)
        entry.bind("<FocusOut>", lambda e: self.commit())
        entry.bind("<Return>", lambda e: self.commit())
        if hint:
            ttk.Label(parent, style="Hint.TLabel", text=hint).grid(row=row, column=2, sticky="w", padx=(4, 0))
        return entry

    def _text(self, parent, key, height=3, width=52):
        text = tk.Text(parent, height=height, width=width, wrap="none", undo=True)
        text.bind("<FocusOut>", lambda e: self.commit())
        self.texts[key] = text
        return text

    def _tower_form(self, parent):
        frame = ttk.LabelFrame(parent, text="The tower", padding=(6, 2))
        frame.pack(fill="x")
        self._entry(frame, 0, "name", "Name")
        self._entry(frame, 1, "label", "Menu button", hint="31 letters at most; TOWER without")
        self._entry(frame, 2, "music", "Music", width=8, hint="a song id, 0x000-0xFFF; the Free Duel screen's without")
        ttk.Label(frame, text="Opens").grid(row=3, column=0, sticky="w")
        self.vars["unlock"] = tk.StringVar(value="always")
        unlock = ttk.Combobox(frame, textvariable=self.vars["unlock"], values=tower.UNLOCKS, state="readonly", width=14)
        unlock.grid(row=3, column=1, sticky="w", padx=(4, 0))
        unlock.bind("<<ComboboxSelected>>", lambda e: self.commit())
        ttk.Label(frame, style="Hint.TLabel", text="after_ending: once the save has seen the ending").grid(
            row=3, column=2, sticky="w", padx=(4, 0))
        conditions = ttk.Frame(frame)
        conditions.grid(row=4, column=0, columnspan=3, sticky="w", pady=(2, 0))
        for column, (key, label, width) in enumerate((("beat", "beat", 14), ("wins", "wins", 4), ("story", "story", 5),
                                                      ("card", "hold", 14), ("copies", "copies", 4))):
            ttk.Label(conditions, text=label).grid(row=0, column=column * 2, sticky="w", padx=(0 if not column else 6, 2))
            var = self.vars["unlock." + key] = tk.StringVar()
            entry = ttk.Entry(conditions, textvariable=var, width=width)
            entry.grid(row=0, column=column * 2 + 1, sticky="w")
            entry.bind("<FocusOut>", lambda e: self.commit())
        self.vars["unlock.after_ending"] = tk.BooleanVar()
        ttk.Checkbutton(conditions, text="and the ending seen", variable=self.vars["unlock.after_ending"],
                        command=self.commit).grid(row=1, column=0, columnspan=6, sticky="w")
        ttk.Label(frame, text="A loss").grid(row=5, column=0, sticky="w")
        self.vars["on_lose"] = tk.StringVar(value="checkpoint")
        lose = ttk.Combobox(frame, textvariable=self.vars["on_lose"], values=("checkpoint", "restart"),
                            state="readonly", width=14)
        lose.grid(row=5, column=1, sticky="w", padx=(4, 0))
        lose.bind("<<ComboboxSelected>>", lambda e: self.commit())
        ttk.Label(frame, style="Hint.TLabel", text="back to the last checkpoint, or to floor 1").grid(
            row=5, column=2, sticky="w", padx=(4, 0))
        story = ttk.LabelFrame(parent, text="Story", padding=(6, 2))
        story.pack(fill="x", pady=(4, 0))
        ttk.Label(story, style="Hint.TLabel", text=STORY_HINT, wraplength=420, justify="left").pack(anchor="w")
        for key, label in (("intro", "Before the first climb"), ("ending", "At the top"), ("lose", "After a loss")):
            ttk.Label(story, text=label).pack(anchor="w")
            self._text(story, key).pack(anchor="w", fill="x")
        only = ttk.LabelFrame(parent, text="The tower's own duelists", padding=(6, 2))
        only.pack(fill="x", pady=(4, 0))
        ttk.Label(only, style="Hint.TLabel", wraplength=420, justify="left",
                  text="Checked: not in Free Duel until a won floor fights it or names it in its reward "
                       "(\"tower_only\"). The mod's added duelists, from the Duelists tab.").pack(anchor="w")
        self.only = tk.Listbox(only, selectmode="multiple", height=5, exportselection=False)
        self.only.pack(anchor="w", fill="x")
        self.only.bind("<<ListboxSelect>>", lambda e: self.commit())
        self.only_keys = []

    def _floors_form(self, parent):
        frame = ttk.LabelFrame(parent, text="Floors", padding=(6, 2))
        frame.pack(fill="x")
        self.list = ttk.Treeview(frame, columns=("duelist", "field", "rules"), height=6, selectmode="browse")
        self.list.heading("#0", text="#")
        self.list.heading("duelist", text="Opponent")
        self.list.heading("field", text="Field")
        self.list.heading("rules", text="Rules")
        self.list.column("#0", width=40)
        self.list.column("duelist", width=180)
        self.list.column("field", width=80)
        self.list.column("rules", width=220)
        self.list.pack(fill="x")
        self.list.bind("<<TreeviewSelect>>", lambda e: self.pick())
        buttons = ttk.Frame(frame)
        buttons.pack(fill="x", pady=(2, 0))
        for label, command in (("Add floor", self.add), ("Remove", self.remove), ("Up", lambda: self.move(-1)),
                               ("Down", lambda: self.move(1))):
            ttk.Button(buttons, text=label, command=command).pack(side="left", padx=(0, 4))
        form = ttk.LabelFrame(parent, text="The floor", padding=(6, 2))
        form.pack(fill="x", pady=(4, 0))
        ttk.Label(form, text="Opponent").grid(row=0, column=0, sticky="w")
        self.vars["duelist"] = tk.StringVar()
        self.duelist_box = ttk.Combobox(form, textvariable=self.vars["duelist"], width=22,
                                        postcommand=self.duelist_choices)
        self.duelist_box.grid(row=0, column=1, sticky="w", padx=(4, 0))
        self.duelist_box.bind("<<ComboboxSelected>>", lambda e: self.commit())
        self.duelist_box.bind("<FocusOut>", lambda e: self.commit())
        ttk.Label(form, text="Field").grid(row=1, column=0, sticky="w")
        self.vars["field"] = tk.StringVar(value="none")
        field = ttk.Combobox(form, textvariable=self.vars["field"], values=tower.TERRAINS, state="readonly", width=12)
        field.grid(row=1, column=1, sticky="w", padx=(4, 0))
        field.bind("<<ComboboxSelected>>", lambda e: self.commit())
        lp = ttk.Frame(form)
        lp.grid(row=2, column=0, columnspan=2, sticky="w")
        for column, (key, label) in enumerate((("lp.player", "Your LP"), ("lp.opponent", "Its LP"))):
            ttk.Label(lp, text=label).grid(row=0, column=column * 2, sticky="w", padx=(0 if not column else 8, 2))
            var = self.vars[key] = tk.StringVar()
            entry = ttk.Entry(lp, textvariable=var, width=7)
            entry.grid(row=0, column=column * 2 + 1)
            entry.bind("<FocusOut>", lambda e: self.commit())
        ttk.Label(lp, style="Hint.TLabel", text="empty: the usual").grid(row=0, column=4, padx=(6, 0))
        flags = ttk.Frame(form)
        flags.grid(row=3, column=0, columnspan=2, sticky="w")
        for key, label in (("no_fusions", "No fusions"), ("no_equips", "No equips"),
                           ("checkpoint", "Checkpoint (a loss after it comes back here)")):
            self.vars[key] = tk.BooleanVar()
            ttk.Checkbutton(flags, text=label, variable=self.vars[key], command=self.commit).pack(side="left",
                                                                                                padx=(0, 8))
        reward = ttk.Frame(form)
        reward.grid(row=4, column=0, columnspan=2, sticky="w", pady=(2, 0))
        for row, (key, label, width, hint) in enumerate((
                ("reward.cards", "Reward cards", 34, "names or ids, a comma between"),
                ("reward.starchips", "Starchips", 8, "the first win only"),
                ("reward.unlock_duelist", "Unlocks", 34, "duelists, a comma between: on the Free Duel grid"))):
            ttk.Label(reward, text=label).grid(row=row, column=0, sticky="w")
            var = self.vars[key] = tk.StringVar()
            entry = ttk.Entry(reward, textvariable=var, width=width)
            entry.grid(row=row, column=1, sticky="w", padx=(4, 0), pady=1)
            entry.bind("<FocusOut>", lambda e: self.commit())
            ttk.Label(reward, style="Hint.TLabel", text=hint).grid(row=row, column=2, sticky="w", padx=(4, 0))
        ttk.Label(form, text="Its deck").grid(row=5, column=0, sticky="nw")
        deck = ttk.Frame(form)
        deck.grid(row=5, column=1, sticky="w", padx=(4, 0))
        self._text(deck, "deck", height=4, width=40).pack(anchor="w")
        ttk.Label(deck, style="Hint.TLabel", text="a card a line, \"Card x3\"; forty in all, or empty for its own"
                  ).pack(anchor="w")
        for row, (key, label) in enumerate((("before", "Before the duel"), ("after", "After a win")), start=6):
            ttk.Label(form, text=label).grid(row=row, column=0, sticky="nw")
            self._text(form, key, height=3, width=46).grid(row=row, column=1, sticky="w", padx=(4, 0), pady=1)

    # --- the floors list ----------------------------------------------------------

    def duelist_choices(self):
        if self.project is not None:
            self.duelist_box.configure(values=duelist_rules.references(self.project))

    def _floor_row(self, index, floor):
        rules = []
        if floor.get("no_fusions") is True:
            rules.append("no fusions")
        if floor.get("no_equips") is True:
            rules.append("no equips")
        if floor.get("life_points") is not None:
            rules.append("LP")
        if floor.get("deck") is not None:
            rules.append("deck")
        if floor.get("checkpoint") is True:
            rules.append("checkpoint")
        field = tower.terrain_of(floor.get("field"))
        return (str(floor.get("duelist", "")), tower.TERRAINS[field] if field is not None else str(floor.get("field")),
                ", ".join(rules))

    def _fill_list(self):
        self.list.delete(*self.list.get_children())
        for index, floor in enumerate(self.floors):
            values = self._floor_row(index, floor) if isinstance(floor, dict) else ("(not an object)", "", "")
            self.list.insert("", "end", iid=str(index), text=str(index + 1), values=values)
        if self.current is not None and self.current < len(self.floors):
            self.list.selection_set(str(self.current))

    def _show_floor(self):
        floor = self.floors[self.current] if self.current is not None and self.current < len(self.floors) else {}
        floor = floor if isinstance(floor, dict) else {}
        self.vars["duelist"].set(str(floor.get("duelist", "")))
        field = tower.terrain_of(floor.get("field"))
        self.vars["field"].set(tower.TERRAINS[field] if field is not None else "none")
        lp = floor.get("life_points")
        sides = lp if isinstance(lp, dict) else {"player": lp, "opponent": lp} if lp is not None else {}
        self.vars["lp.player"].set("" if sides.get("player") is None else str(sides["player"]))
        self.vars["lp.opponent"].set("" if sides.get("opponent") is None else str(sides["opponent"]))
        for key in ("no_fusions", "no_equips", "checkpoint"):
            self.vars[key].set(floor.get(key) is True)
        reward = floor.get("reward") if isinstance(floor.get("reward"), dict) else {}
        cards = []
        for key in ("card", "cards"):
            value = reward.get(key)
            cards += value if isinstance(value, list) else [] if value is None else [value]
        self.vars["reward.cards"].set(tower.names_text(cards))
        self.vars["reward.starchips"].set("" if reward.get("starchips") is None else str(reward["starchips"]))
        self.vars["reward.unlock_duelist"].set(tower.names_text(reward.get("unlock_duelist")))
        self._set_text("deck", tower.deck_text(floor.get("deck")))
        self._set_text("before", tower.story_text(floor.get("before")))
        self._set_text("after", tower.story_text(floor.get("after")))
        self.shown = (self.current, self._form(self.FLOOR_FIELDS, self.FLOOR_TEXTS))

    def _set_text(self, key, value):
        text = self.texts[key]
        text.delete("1.0", "end")
        text.insert("1.0", value)
        text.edit_reset()

    def _get_text(self, key) -> str:
        return self.texts[key].get("1.0", "end-1c")

    def _read_floor(self) -> dict:
        """The form's floor, over the floor's own keys the form does not show."""
        old = self.floors[self.current] if self.current is not None and self.current < len(self.floors) else {}
        old = old if isinstance(old, dict) else {}
        floor = {k: tower.copy(v) for k, v in old.items() if k not in tower.FLOOR_KEYS}
        duelist = self.vars["duelist"].get().strip()
        floor["duelist"] = int(duelist) if duelist.isdigit() else duelist
        field = self.vars["field"].get()
        if field and field != "none":
            # The terrain as the mod wrote it ("Yami" is "Dark") while it is the same one.
            same = "field" in old and tower.terrain_of(old["field"]) == tower.terrain_of(field)
            floor["field"] = old["field"] if same else field
        sides = {}
        for side in ("player", "opponent"):
            text = self.vars["lp." + side].get().strip()
            if text:
                sides[side] = int(text) if text.isdigit() else text
        if sides:
            floor["life_points"] = sides["player"] if len(sides) == 2 and sides["player"] == sides["opponent"] \
                else sides
        for key in ("no_fusions", "no_equips"):
            if self.vars[key].get():
                floor[key] = True
        deck = tower.deck_value(self._get_text("deck"))
        if deck is not None:
            floor["deck"] = deck if old.get("deck") is None or tower.deck_value(tower.deck_text(old["deck"])) != deck \
                else old["deck"]
        reward = {k: tower.copy(v) for k, v in (old.get("reward") or {}).items()
                  if isinstance(old.get("reward"), dict) and k not in tower.REWARD_KEYS}
        cards = tower.names_value(self.vars["reward.cards"].get())
        if cards is not None:
            reward["cards"] = cards if isinstance(cards, list) else [cards]
        chips = self.vars["reward.starchips"].get().strip()
        if chips:
            reward["starchips"] = int(chips) if chips.isdigit() else chips
        duelists = tower.names_value(self.vars["reward.unlock_duelist"].get())
        if duelists is not None:
            reward["unlock_duelist"] = duelists
        if reward:
            floor["reward"] = reward
        if self.vars["checkpoint"].get():
            floor["checkpoint"] = True
        for key in ("before", "after"):
            value = tower.story_value(self._get_text(key))
            if value is not None:
                floor[key] = old[key] if tower.same_story(old.get(key), self._get_text(key)) else value
        order = list(tower.FLOOR_KEYS)
        return {**{k: floor[k] for k in order if k in floor}, **{k: v for k, v in floor.items() if k not in order}}

    def _store_floor(self):
        if self.current is not None and self.current < len(self.floors):
            if self.shown == (self.current, self._form(self.FLOOR_FIELDS, self.FLOOR_TEXTS)):
                return
            self.floors[self.current] = self._read_floor()
            self.shown = (self.current, self._form(self.FLOOR_FIELDS, self.FLOOR_TEXTS))

    def pick(self):
        chosen = self.list.selection()
        if not chosen or int(chosen[0]) == self.current:
            return
        self.commit()
        self.current = int(chosen[0])
        self._show_floor()

    def add(self):
        if self.project is None:
            return
        self._store_floor()
        if len(self.floors) >= tower.TOWER_MAX_FLOORS:
            self.status.configure(text=f"A tower has at most {tower.TOWER_MAX_FLOORS} floors")
            return
        self.has.set(True)
        self.floors.append(tower.new_floor())
        self.current = len(self.floors) - 1
        self._fill_list()
        self._show_floor()
        self.commit()

    def remove(self):
        if self.current is None or self.current >= len(self.floors):
            return
        del self.floors[self.current]
        self.current = min(self.current, len(self.floors) - 1) if self.floors else None
        self._fill_list()
        self._show_floor()
        self.commit(stored=True)

    def move(self, step):
        if self.current is None:
            return
        self._store_floor()
        to = self.current + step
        if not 0 <= to < len(self.floors):
            return
        self.floors[self.current], self.floors[to] = self.floors[to], self.floors[self.current]
        self.current = to
        if self.shown is not None:
            self.shown = (to, self.shown[1])   # the same floor, still in the form
        self._fill_list()
        self.commit(stored=True)

    def toggle(self):
        if self.project is None:
            return
        if self.has.get() and not self.floors:
            self.floors = [tower.new_floor()]
            self.current = 0
            self._fill_list()
            self._show_floor()
        self.commit()

    # --- the tab --------------------------------------------------------------------

    def refresh(self):
        if self.project is None:
            return
        value = self.project.other.get("tower")
        written = value if isinstance(value, dict) else {}
        self.has.set(value is not None)
        self.written = value
        self.kept ={k: tower.copy(v) for k, v in written.items() if k not in tower.TOWER_KEYS}
        self.vars["name"].set(str(written.get("name", "")))
        self.vars["label"].set(str(written.get("label", "")))
        self.vars["music"].set("" if written.get("music") is None else str(written["music"]))
        unlock = written.get("unlock")
        self.vars["unlock"].set("conditions" if isinstance(unlock, dict) else
                                unlock if unlock in ("always", "after_ending") else "always")
        conditions = unlock if isinstance(unlock, dict) else {}
        for key in ("beat", "wins", "story", "card", "copies"):
            self.vars["unlock." + key].set("" if conditions.get(key) is None else str(conditions[key]))
        self.vars["unlock.after_ending"].set(conditions.get("after_ending") is True)
        self.vars["on_lose"].set(written.get("on_lose") if written.get("on_lose") in ("checkpoint", "restart")
                                 else "checkpoint")
        for key in ("intro", "ending", "lose"):
            self._set_text(key, tower.story_text(written.get(key)))
        floors = written.get("floors")
        self.floors = [tower.copy(f) if isinstance(f, dict) else f for f in floors] if isinstance(floors, list) else []
        self.current = 0 if self.floors else None
        self._fill_list()
        self._show_floor()
        self.top_shown = self._form(self.TOP_FIELDS, self.TOP_TEXTS)
        self.only_keys = [e.key for e in roster.copies(self.project)]
        self.only.delete(0, "end")
        marked = set(tower.tower_only(self.project))
        for index, key in enumerate(self.only_keys):
            self.only.insert("end", f"{roster.identity(self.project, roster.find(self.project, key))}")
            if key in marked:
                self.only.selection_set(index)
        self._report()

    def _build(self) -> dict | None:
        if not self.has.get():
            return None
        written = self.written if isinstance(self.written, dict) else None
        if written is not None and self.top_shown == self._form(self.TOP_FIELDS, self.TOP_TEXTS):
            # The tower's own fields untouched: only the floors can change.
            out = tower.copy(written)
            if "floors" in out or self.floors:
                out["floors"] = [tower.copy(f) for f in self.floors]
            return out
        out = {}
        for key in ("name", "label"):
            text = self.vars[key].get().strip()
            if text:
                out[key] = text
        music = self.vars["music"].get().strip()
        if music:
            try:
                out["music"] = int(music, 0)
            except ValueError:
                out["music"] = music
        kind = self.vars["unlock"].get()
        if kind == "after_ending":
            out["unlock"] = "after_ending"
        elif kind == "conditions":
            conditions = {}
            if self.vars["unlock.after_ending"].get():
                conditions["after_ending"] = True
            for key in ("beat", "wins", "story", "card", "copies"):
                text = self.vars["unlock." + key].get().strip()
                if text:
                    conditions[key] = int(text) if text.isdigit() else text
            out["unlock"] = conditions
        if self.vars["on_lose"].get() == "restart":
            out["on_lose"] = "restart"
        for key in ("intro", "ending", "lose"):
            value = tower.story_value(self._get_text(key))
            if value is not None:
                old = written.get(key) if written is not None else None
                out[key] = old if tower.same_story(old, self._get_text(key)) else value
        out["floors"] = [tower.copy(f) for f in self.floors]
        out.update(self.kept)
        return out

    def _report(self):
        if self.project is None:
            return
        problems = tower.check(self.project.other.get("tower"), lambda name: roster.named(self.project, name),
                               lambda name: self.project.resolve(name))
        self.status.configure(text="\n".join(f"{level}: {where}: {message}" for level, where, message in problems))
        for index, floor in enumerate(self.floors):
            if isinstance(floor, dict) and self.list.exists(str(index)):
                self.list.item(str(index), values=self._floor_row(index, floor))

    def commit(self, stored=False):
        if self.project is None:
            return True
        if not stored:
            self._store_floor()
        before = self.project.other.get("tower")
        after = self._build()
        # Untouched, the tower stays as the mod wrote it: every tab switch
        # commits, and rebuilding it would reorder its keys and mark the mod
        # changed.
        if after is not None and before is not None and tower.copy(after) == before:
            after = before
        if after is None:
            self.project.other.pop("tower", None)
        else:
            self.project.other["tower"] = after
        changed = after != before
        selected = {self.only_keys[i] for i in self.only.curselection()} if self.only_keys else set()
        for key in self.only_keys:
            changed = tower.set_tower_only(self.project, key, key in selected) or changed
        self._report()
        if changed:
            self.app.changed()
        self.applied()
        return True
