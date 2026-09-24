"use client";

import { useEffect, useState } from "react";
import { useRouter } from "next/navigation";
import { useAuth } from "@/components/auth-provider";
import RiskAssessment from "@/components/risk-assessment";

export default function WelcomePage() {
  const router = useRouter();
  const { user, loading, logout } = useAuth();

  useEffect(() => {
    if (!loading && !user) router.replace("/login");
  }, [loading, router, user]);

  if (loading || !user) return <main className="flex min-h-screen items-center justify-center bg-[#0a1828]" />;
  return <RiskAssessment />;
}
