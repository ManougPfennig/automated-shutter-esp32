/*
 * web_server.h
 *
 * HTTP server serving the control page and its JSON API:
 *   GET  /                  control page
 *   GET  /api/status        shutter state + schedule
 *   POST /api/settings      save open_time / close_time
 *   POST /api/schedule      {"enabled": bool}: turn the schedule on/off
 *   POST /api/manual/<a>    a = open | close | stop
 *
 * There is no login/authentication: keep it on your local network.
 */
#pragma once

void web_server_start(void);
