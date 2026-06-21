/* =============================================================================
 *  GREJEM OS / heartbeat.h
 *  Periodyczny pakiet "żyję" wysyłany do PC (co 1.5 s).
 * ============================================================================= */
#ifndef GREJEM_HEARTBEAT_H
#define GREJEM_HEARTBEAT_H

void heartbeat_init(void);

/* Wywoływane przez scheduler co CFG_HEARTBEAT_PERIOD_MS.
 * Emituje PROTO_HEARTBEAT i przełącza diodę statusową (wskazuje "żyję"). */
void heartbeat_tick(void);

#endif /* GREJEM_HEARTBEAT_H */
