# Emergency notifications: the contract with the signal controller

What the Saloon Watchkeeper displays write to and read from Signal K for a declared emergency. The signal controller (horn, bell, nav lights; a separate project) reads the same notifications and decides the signals per alarm type. Design: `docs/specs/2026-11-06-emergency-design.md` (local, not in git).

**Status: provisional.** Everything marked *unconfirmed* has not been checked against the Signal K specification, the server in use, or the signal controller. Nothing here has been run against a real server.

## What a display does

A display raises an emergency only when a person holds a button for one second on the emergency script page (Navigation page 2). Nothing is sent on a tap. The buttons are not shown unless Signal K is connected and the Navigation role is configured.

| Declared nature | Notification path | Message |
|---|---|---|
| Man overboard | `notifications.mob` | `MAN OVERBOARD` |
| Fire | `notifications.fire` | `FIRE` |
| Flooding | `notifications.flooding` | `FLOODING` |
| Sinking | `notifications.sinking` | `SINKING` |
| Other | `notifications.emergency` | `EMERGENCY` (*unconfirmed*: not a Signal K spec path; the name is provisional) |

Path names are from memory of the Signal K specification (*unconfirmed*).

### The request

`POST /signalk/v2/api/notifications/<nature>` with a 3 s deadline, one attempt, no queue and no retry (*unconfirmed*: the endpoint and whether it accepts every nature and this body). A late man-overboard would put the waypoint in the wrong place, so a send that does not get through is reported loudly on the display (`NOT SENT - TELL HELM`) and never repeated.

```json
{
  "state": "emergency",
  "method": ["visual", "sound"],
  "message": "MAN OVERBOARD",
  "data": {
    "position": { "latitude": 41.540283, "longitude": -70.25 },
    "time": "2023-11-14T22:13:20Z"
  }
}
```

- `data` is sent only for a MOB, and only when the display had a GPS fix no older than 10 s at the press. Without a fix the notification carries no `data`; displays then show `MOB NO POSITION` and draw no arrow. *Unconfirmed*: the field names `data.position.latitude`, `data.position.longitude` and `data.time` are this project's choice and have to be agreed with the signal controller.
- Position and time are those at the moment of the press, not at the moment of sending.
- Cancel: the same path with `{"state": "normal", "method": [], "message": "<MESSAGE> CANCELLED"}`. The display keeps showing the emergency until the server has accepted the cancel; a cancel that cannot be sent is reported the same way.

### The waypoint (MOB only, best effort)

`POST /signalk/v2/api/resources/waypoints`, GeoJSON Point, longitude first, named `MOB hh:mm:ss` (UTC):

```json
{ "name": "MOB 22:13:20",
  "feature": { "type": "Feature", "geometry": { "type": "Point", "coordinates": [-70.25, 41.540283] }, "properties": {} } }
```

*Unconfirmed*: the body against the server's OpenAPI definition and the resource provider in use. A failure of the waypoint never raises the send alarm. This reaches Signal K clients such as Freeboard-SK only. Whether a hardware plotter drops a waypoint is up to the plotter or the server (research notes in the spec: no plotter was found that creates one from a Signal K notification).

## What a display reads

Any notification whose path is one of the five above, on this vessel, with `state` `alarm` or `emergency` declares that nature on every display; `normal`, `null` or any other state clears it. A MOB takes its target from `data.position` in the notification, never from the receiving display's own position. A different nature never displaces an active MOB on screen. The ordinary alert handling also raises the alarm banner and buzzer for the notification.

## What a display does not do

- It sends nothing on NMEA 2000 and never will from this project (listen-only; see `docs/nmea2000-plan.md`).
- It does not command the horn, bell or lights. Those are the signal controller's job, driven by the notifications above.
- It does not search for a MOB position anywhere else.

## To confirm on the bench

1. The endpoint path and whether the server accepts a notification with `data`, for all five natures.
2. Write permission for `notifications.*` and `resources/waypoints` with the display's access token (a refusal is reported as `NO WRITE ACCESS`).
3. Field names with the signal controller project; the name of the "other" path.
