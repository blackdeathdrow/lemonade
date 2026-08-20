import React, { useEffect, useState } from 'react';
import { getClientSessionId, serverFetch } from './utils/serverConfig';

interface SessionRequest {
  model?: string;
  kind?: string;
  streaming?: boolean;
}

interface ActiveSession {
  client_session_id?: string;
  key?: string;
  client_app?: string;
  client_name?: string;
  remote_addr?: string;
  remote_port?: number;
  user_agent?: string;
  authenticated?: boolean;
  is_polling?: boolean;
  request_count?: number;
  last_active_ms?: number;
  active_requests?: SessionRequest[];
}

interface WebSocketConnection {
  connection_id?: string;
  kind?: string;
  client_session_id?: string;
  authenticated?: boolean;
  remote_addr?: string;
  user_agent?: string;
  model?: string;
  connected_ms?: number;
}

interface ConnectionsPayload {
  sessions: ActiveSession[];
  websockets: WebSocketConnection[];
}

function formatAge(ms: number): string {
  const seconds = Math.max(0, Math.floor(ms / 1000));
  if (seconds < 60) return `${seconds}s`;
  const minutes = Math.floor(seconds / 60);
  if (minutes < 60) return `${minutes}m`;
  const hours = Math.floor(minutes / 60);
  return `${hours}h`;
}

function clientLabel(session: ActiveSession | WebSocketConnection): string {
  const app = 'client_app' in session ? session.client_app : '';
  const agent = session.user_agent || '';
  if (app && app !== '') return app;
  if (agent === '') return 'unknown client';
  if (agent.startsWith('Mozilla/')) {
    const match = agent.match(/Chrome\/(\d+)|Firefox\/(\d+)|Safari\/(\d+)/);
    if (match) {
      if (agent.includes('Chrome')) return `Chrome ${match[1]}`;
      if (agent.includes('Firefox')) return `Firefox ${match[1]}`;
      return 'Safari';
    }
    return 'Browser';
  }
  return agent.split('/')[0] || agent;
}

const SessionsPanel: React.FC = () => {
  const [payload, setPayload] = useState<ConnectionsPayload>({ sessions: [], websockets: [] });
  const [error, setError] = useState<string | null>(null);
  const [now, setNow] = useState<number>(Date.now());

  useEffect(() => {
    let isMounted = true;

    const load = async () => {
      try {
        const response = await serverFetch('/connections');
        if (!response.ok) {
          throw new Error(`HTTP ${response.status}`);
        }
        const data = await response.json();
        if (!isMounted) return;
        setPayload({
          sessions: Array.isArray(data.sessions) ? data.sessions : [],
          websockets: Array.isArray(data.websockets) ? data.websockets : [],
        });
        setError(null);
      } catch (err) {
        if (!isMounted) return;
        setError(err instanceof Error ? err.message : 'Unknown error');
      }
    };

    load();
    const interval = setInterval(load, 3000);
    const clock = setInterval(() => setNow(Date.now()), 1000);
    return () => {
      isMounted = false;
      clearInterval(interval);
      clearInterval(clock);
    };
  }, []);

  const ownSessionId = getClientSessionId();
  const totalActive = payload.websockets.length + payload.sessions.filter((s) => (s.active_requests?.length ?? 0) > 0).length;
  const sortedSessions = [...payload.sessions].sort((a, b) => (b.last_active_ms ?? 0) - (a.last_active_ms ?? 0));

  return (
    <div className="sessions-panel widget">
      <div className="sessions-summary">
        <span className="loaded-model-label">ACTIVE CONNECTIONS</span>
        <div className="loaded-model-count-pill">{totalActive} active</div>
      </div>
      {error && <div className="loaded-model-empty">Unable to reach /connections: {error}</div>}
      {!error && payload.websockets.length === 0 && sortedSessions.length === 0 && (
        <div className="loaded-model-empty">No active clients</div>
      )}

      {payload.websockets.length > 0 && (
        <div className="sessions-group">
          <div className="sessions-group-title">WEB SOCKETS</div>
          {payload.websockets.map((conn) => (
            <div key={conn.connection_id} className="session-entry">
              <div className="session-entry-header">
                <span className="session-entry-name" title={conn.user_agent || ''}>
                  {clientLabel(conn)}
                </span>
                <span className={`session-badge ${conn.authenticated ? 'session-badge-auth' : 'session-badge-unauth'}`}>
                  {conn.authenticated ? 'auth' : 'no-auth'}
                </span>
              </div>
              <div className="session-entry-detail">
                <span className={`session-kind session-kind-${conn.kind || ''}`}>{conn.kind || 'ws'}</span>
                <span>{conn.model ? `model: ${conn.model}` : ''}</span>
                <span>{conn.remote_addr || ''}</span>
                <span>{formatAge((now ?? Date.now()) - (conn.connected_ms ?? now))}</span>
                {conn.client_session_id === ownSessionId && <span className="session-this-device">this device</span>}
              </div>
            </div>
          ))}
        </div>
      )}

      {sortedSessions.length > 0 && (
        <div className="sessions-group">
          <div className="sessions-group-title">HTTP</div>
          {sortedSessions.map((session, index) => {
            const requests = session.active_requests ?? [];
            const isPolling = session.is_polling === true && requests.length === 0;
            return (
              <div key={session.key ?? index} className="session-entry">
                <div className="session-entry-header">
                  <span className="session-entry-name" title={session.user_agent || ''}>
                    {clientLabel(session)}
                  </span>
                  <span className={`session-badge ${session.authenticated ? 'session-badge-auth' : 'session-badge-unauth'}`}>
                    {session.authenticated ? 'auth' : 'no-auth'}
                  </span>
                </div>
                <div className="session-entry-detail">
                  <span>{session.remote_addr ? `${session.remote_addr}:${session.remote_port ?? ''}` : ''}</span>
                  <span>{requests.length > 0 ? `inferring: ${requests.map((r) => r.model || r.kind || '?').join(', ')}` : ''}</span>
                  {isPolling && <span className="session-polling">polling</span>}
                  <span>{formatAge((now ?? Date.now()) - (session.last_active_ms ?? now))} ago</span>
                  {session.client_session_id === ownSessionId && <span className="session-this-device">this device</span>}
                </div>
              </div>
            );
          })}
        </div>
      )}
    </div>
  );
};

export default SessionsPanel;
