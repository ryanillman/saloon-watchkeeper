# Emergency notifications: the contract with the signal controller

What the Saloon Watchkeeper displays write to and read from Signal K for a declared emergency. The signal controller (horn, bell, nav lights; a separate project) reads the same notifications and decides the signals per alarm type.

**Verified** below means checked against the Signal K server on the bench (signalk-server 2.35.1, 2026-11-06, its source and live requests). **Unconfirmed** means not yet. The first version of this contract assumed a per-nature endpoint (`POST /notifications/fire`); it does not exist (404), which is why a fire from a display did not send. The server only has a per-nature endpoint for `mob`.

## What a display does

A display raises an emergency only when a person holds a button for one second on the emergency script page (Navigation page 2). Nothing is sent on a tap. The buttons are not shown unless Signal K is connected and the Navigation role is configured.

### Raise (verified)

`POST /signalk/v2/api/notifications`, one attempt, 3 s deadline, no queue and no retry. A late man-overboard would put the waypoint in the wrong place, so a send that does not get through is reported loudly on the display (`NOT SENT - TELL HELM`) and never repeated.

```json
{ "state": "emergency", "message": "MAN OVERBOARD", "path": "mob", "idInPath": true, "includeCreatedAt": true,
  "data": { "position": { "latitude": 41.540283, "longitude": -70.25 }, "time": "2023-11-14T22:13:20Z" } }
```

The server creates the notification at `notifications.<path>.<uuid>` (because of `idInPath`) and replies `{"state":"COMPLETED","statusCode":200,"id":"<uuid>"}`. The display keeps that id: it is the only way to clear the notification. Because the state is `emergency` the server also adds `value.position` (its own position when it handled the request), `value.createdAt` and `value.id`, and the method defaults to `["visual","sound"]`; `data` is kept as sent.

| Nature | `path` | `message` |
|---|---|---|
| Man overboard | `mob` | `MAN OVERBOARD` |
| Fire | `fire` | `FIRE` |
| Flooding | `flooding` | `FLOODING` |
| Sinking | `sinking` | `SINKING` |
| Other | `emergency` | `EMERGENCY` (**unconfirmed**: not a Signal K spec path; the name is provisional) |

`data` is sent only for a MOB, and only when the display had a GPS fix no older than 10 s at the press. Position and time are those at the moment of the press, not when the request was handled. Without a fix the MOB carries no `data` and displays show `MOB NO POSITION` (the server's own `value.position` is then used if it has one). **Unconfirmed**: the field names `data.position.latitude`, `data.position.longitude` and `data.time` are this project's choice and have to be agreed with the signal controller.

The server's own `POST /signalk/v2/api/notifications/mob` (the helm's button, plugins) produces the same path shape, `notifications.mob.<uuid>`, with the position in `value.position` and no `data`. Displays treat both alike.

### Cancel (verified)

`PUT /signalk/v2/api/notifications/<uuid>` with `{"state":"normal"}`, using the id the raise returned (or `value.id` of a notification raised by someone else). The notification stays in the server's list with state `normal`. `DELETE` on the same URL also works for notifications raised through this API (`canClear`), but not for ones that arrived as a plain delta, and the display's HTTP transport does not do DELETE, so PUT is used. Never `POST .../mob` to cancel: it raises another MOB.

The display keeps showing the emergency until the server has accepted the cancel. A cancel that cannot be sent (no id known yet, offline, refused) is reported the same way and the emergency stays declared.

### The waypoint (MOB only, best effort; verified)

`POST /signalk/v2/api/resources/waypoints`, GeoJSON Point, longitude first, named `MOB hh:mm:ss` (UTC); the server replies 201 with an id:

```json
{ "name": "MOB 22:13:20",
  "feature": { "type": "Feature", "geometry": { "type": "Point", "coordinates": [-70.25, 41.540283] }, "properties": {} } }
```

A failure of the waypoint never raises the send alarm. This reaches Signal K clients such as Freeboard-SK only; whether a hardware plotter drops a waypoint is up to the plotter or the server.

## What a display reads

Any notification on this vessel whose path below `notifications.` is `mob`, `fire`, `flooding`, `sinking` or `emergency`, optionally followed by `.<anything>` (the server's uuid), with `state` `alarm` or `emergency`, declares that nature on every display. `normal`, `null` or any other state clears it, but a `normal` carrying a different `value.id` than the one a display holds is about another notification and is ignored. A MOB takes its position from `value.data.position`, else `value.position`; never from the receiving display's own position. A different nature never displaces an active MOB on screen. The ordinary alert handling also raises the alarm banner and buzzer for the notification.

## What a display does not do

- It sends nothing on NMEA 2000 and never will from this project (listen-only; see `docs/nmea2000-plan.md`).
- It does not command the horn, bell or lights. Those are the signal controller's job, driven by the notifications above.
- It does not search for a MOB position anywhere else.

## To confirm

1. Field names `data.position` and `data.time` and the "other" path name with the signal controller project.
2. That the signal controller subscribes to `notifications.mob.*` (not `notifications.mob`): the server's paths carry the uuid.
3. Behaviour with a read-only token (the display reports `NO WRITE ACCESS`).
