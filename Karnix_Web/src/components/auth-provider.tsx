"use client";

import { createContext, useContext, useEffect, useState } from "react";
import { api, ApiError, type User } from "@/lib/api";

type AuthContextValue = {
  user: User | null;
  loading: boolean;
  login: (email: string, password: string) => Promise<void>;
  register: (name: string, email: string, password: string, confirmPassword: string) => Promise<void>;
  logout: () => Promise<void>;
};

const AuthContext = createContext<AuthContextValue | null>(null);

export function AuthProvider({ children }: { children: React.ReactNode }) {
  const [user, setUser] = useState<User | null>(null);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    let active = true;
    api.me()
      .then(({ user: currentUser }) => active && setUser(currentUser))
      .catch((error) => {
        if (active && (!(error instanceof ApiError) || error.status !== 401)) setUser(null);
      })
      .finally(() => active && setLoading(false));
    return () => { active = false; };
  }, []);

  const value: AuthContextValue = {
    user,
    loading,
    async login(email, password) {
      const result = await api.login({ email, password });
      setUser(result.user);
    },
    async register(name, email, password, confirmPassword) {
      const result = await api.register({ name, email, password, confirmPassword });
      setUser(result.user);
    },
    async logout() {
      try {
        await api.logout();
      } finally {
        setUser(null);
      }
    },
  };

  return <AuthContext.Provider value={value}>{children}</AuthContext.Provider>;
}

export function useAuth() {
  const context = useContext(AuthContext);
  if (!context) throw new Error("useAuth must be used within AuthProvider");
  return context;
}
