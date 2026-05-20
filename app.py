from fastapi import FastAPI, WebSocket, HTTPException
from pydantic import BaseModel, Field  # Field added
from fastapi.responses import JSONResponse, FileResponse
from fastapi.staticfiles import StaticFiles
from typing import List
from uuid import UUID, uuid4
from fastapi.middleware.cors import CORSMiddleware


app = FastAPI()

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],  # or your frontend IP/domain
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

latest_esp_data = []
user_settings = []               # separate store for user outlet settings
connected_clients: List[WebSocket] = []
esp_state = []
esp_confirmed_settings = []

MAX_HISTORY = 50                 # cap on stored readings


# ── Data Models ──────────────────────────────────────────────────────────────

class Power_Strip_data(BaseModel):
    interval_eng_Wh: List[float]       # real-time energy per socket (W)
    daily_eng_consum: List[float]    # cumulative daily consumption per socket (Wh)
    outlet_on_off: List[bool] = None
    outlet_current_limit: List[float] = None
    interval_log_fired: bool = False    # ← ADD THIS


class GraphResponse(BaseModel):
    # FIX 1: was `daily_eng_consum_graph[float]` — missing field name & colon
    daily_eng_consum_graph: List[float]


class User_outlet_input(BaseModel):
    outlet_current_limit: List[float]   # current limit per socket (A)
    # FIX 2: Field now imported; uuid4 generates a unique ID for each request
    socket_ID: UUID = Field(default_factory=uuid4)
    # FIX 3: Bool → bool  (Python built-in)
    outlet_on_off: List[bool]


# ── Static files & webpage ────────────────────────────────────────────────────

app.mount("/static", StaticFiles(directory="static"), name="static")

@app.get("/")
async def get_webpage():
    return FileResponse("static/index.html")


# ── WebSocket (real-time push to browser) ────────────────────────────────────

@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    connected_clients.append(websocket)
    # FIX 4: try block was wrongly indented (extra tab)
    try:
        while True:
            # FIX 5: recieve_text → receive_text  (typo)
            await websocket.receive_text()
    except Exception:
        connected_clients.remove(websocket)


# ── Energy data (ESP → server) ────────────────────────────────────────────────
@app.post("/energy_data")
async def calculated_energy_data(energy_request: Power_Strip_data):
    latest_esp_data.append(energy_request)
    if len(latest_esp_data) > MAX_HISTORY:
        latest_esp_data.pop(0)

    # CHANGED: write to esp_confirmed_settings, NOT user_settings
    if energy_request.outlet_on_off is not None and energy_request.outlet_current_limit is not None:
        confirmed = User_outlet_input(
            outlet_on_off=energy_request.outlet_on_off,
            outlet_current_limit=energy_request.outlet_current_limit
        )
        esp_confirmed_settings.append(confirmed)
        if len(esp_confirmed_settings) > MAX_HISTORY:
            esp_confirmed_settings.pop(0)

    for client in connected_clients:
        await client.send_json(energy_request.dict())

    return JSONResponse(status_code=200, content={"status": "ok"})

@app.get("/energy_data")
async def get_calc_energy_data():
    if not latest_esp_data:
        # FIX 8: missing closing quote and brace in original string literal
        return JSONResponse(status_code=404, content={"error": "No data yet"})
    
    data = latest_esp_data[-1].dict()

    if user_settings:
        last = user_settings[-1]
        data["outlet_on_off"] = last.outlet_on_off
        data["outlet_current_limit"] = last.outlet_current_limit

    return latest_esp_data[-1].dict()


@app.get("/energy_history")
async def get_energy_history():
    """Return all stored readings so the frontend can draw a full graph."""
    if not latest_esp_data:
        return JSONResponse(status_code=404, content={"error": "No data yet"})
    return [entry.dict() for entry in latest_esp_data]


# ── User outlet control (browser → server) ────────────────────────────────────

@app.post("/user_outlet_input")
async def post_user_outlet_input(output_request: User_outlet_input):  # FIX 9: type was `outlet_on_off` (undefined)
    # FIX 10: HTTPException now imported at the top
    # Validate list lengths match if we already have ESP data
    if latest_esp_data:
        num_sockets = len(latest_esp_data[-1].interval_eng_Wh)
        if len(output_request.outlet_on_off) != num_sockets:
            raise HTTPException(
                status_code=400,
                detail=f"outlet_on_off length must match number of sockets ({num_sockets})"
            )
        if len(output_request.outlet_current_limit) != num_sockets:
            raise HTTPException(
                status_code=400,
                detail=f"outlet_current_limit length must match number of sockets ({num_sockets})"
            )
    
    user_settings.append(output_request)
    if len(user_settings) > MAX_HISTORY:
        user_settings.pop(0)

    # Push new settings to all connected browser clients
    for client in connected_clients:
         await client.send_json(output_request.dict(exclude={"socket_ID"})) 

    return JSONResponse(status_code=200, content={"status": "settings applied"})


@app.get("/user_outlet_input")
async def get_user_outlet_input():
    """Let the frontend retrieve the last-applied outlet settings."""
    if not user_settings:
        return JSONResponse(status_code=404, content={"error": "No settings yet"})
    return user_settings[-1].dict()


# ── Socket status helper endpoint ─────────────────────────────────────────────

# FIX 11: was a bare async def (not a route) using wrong variable scope and
#          C-style param syntax `UUID socket_ID`.  Now a proper GET route.
@app.get("/outlet_status/{socket_index}")
async def outlet_status(socket_index: int):
    """Return on/off state of a specific socket by its list index."""
    if not user_settings:
        raise HTTPException(status_code=404, detail="No settings yet")

    latest = user_settings[-1]

    if socket_index < 0 or socket_index >= len(latest.outlet_on_off):
        raise HTTPException(status_code=400, detail="socket_index out of range")

    # FIX 12: `= true` (assignment + wrong casing) → `== True`
    socket_state = "on" if latest.outlet_on_off[socket_index] == True else "off"

    return {
        "socket_index": socket_index,
        "socket_state": socket_state,
        "current_limit": latest.outlet_current_limit[socket_index],
    }

@app.post("/esp_state")
async def post_esp_state(state: User_outlet_input):
    esp_state.append(state)
    if len (esp_state) > MAX_HISTORY:
        esp_state.pop(0)
    return JSONResponse(status_code=200, content={"status": "esp state recorded"})