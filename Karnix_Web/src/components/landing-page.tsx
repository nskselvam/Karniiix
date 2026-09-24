"use client";

import { useEffect, useState, type FormEvent } from "react";
import Link from "next/link";
import { useRouter } from "next/navigation";
import { Eye, EyeOff, LoaderCircle } from "lucide-react";
import { useAuth } from "@/components/auth-provider";
import { ApiError } from "@/lib/api";

const treePositions = [
  { left: "5%", width: 180, height: 360, bottom: 0, rotation: -8 },
  { left: "15%", width: 170, height: 330, bottom: 0, rotation: 12 },
  { left: "23%", width: 220, height: 430, bottom: 0, rotation: -10 },
  { left: "38%", width: 210, height: 420, bottom: 0, rotation: 8 },
  { left: "55%", width: 200, height: 390, bottom: 0, rotation: -12 },
  { left: "67%", width: 190, height: 400, bottom: 0, rotation: 14 },
  { left: "78%", width: 220, height: 430, bottom: 0, rotation: -10 },
  { left: "89%", width: 200, height: 360, bottom: 0, rotation: 6 },
];

export default function LandingPage({ mode = "login" }: { mode?: "login" | "register" }) {
  const router = useRouter();
  const { user, loading, login, register } = useAuth();
  const [submitting, setSubmitting] = useState(false);
  const [showPassword, setShowPassword] = useState(false);
  const [error, setError] = useState("");

  useEffect(() => {
    if (!loading && user) router.replace("/welcome");
  }, [loading, router, user]);

  async function handleSubmit(event: FormEvent<HTMLFormElement>) {
    event.preventDefault();
    setError("");
    setSubmitting(true);
    const data = new FormData(event.currentTarget);
    try {
      const email = String(data.get("email") ?? "");
      const password = String(data.get("password") ?? "");
      if (mode === "register") {
        await register(String(data.get("name") ?? ""), email, password, String(data.get("confirmPassword") ?? ""));
      } else {
        await login(email, password);
      }
      router.replace("/welcome");
    } catch (caught) {
      setError(caught instanceof ApiError ? caught.message : "Something went wrong. Please try again.");
    } finally {
      setSubmitting(false);
    }
  }

  if (loading || user) {
    return <div className="flex min-h-screen items-center justify-center bg-[#0a1828]"><LoaderCircle className="h-7 w-7 animate-spin" aria-label="Loading" /></div>;
  }

  return (
    <div className="relative min-h-screen overflow-hidden bg-[#0a1828] text-slate-100">
      <div className="absolute inset-0 bg-[radial-gradient(circle_at_center,_rgba(155,180,200,0.28),_transparent_50%)]" />

      <div className="absolute inset-x-0 top-0 z-0 h-full bg-[linear-gradient(180deg,rgba(7,15,25,0.75),rgba(11,24,37,0.65))]" />

      <div className="absolute inset-0 z-0 opacity-90">
        {treePositions.map((tree, index) => (
          <div
            key={index}
            className="absolute rounded-t-[100%] bg-[#0a2135] shadow-[inset_0_0_0_1px_rgba(255,255,255,0.02)]"
            style={{
              left: tree.left,
              bottom: tree.bottom,
              width: `${tree.width}px`,
              height: `${tree.height}px`,
              transform: `rotate(${tree.rotation}deg)`,
              clipPath: "polygon(25% 0%, 50% 10%, 62% 0%, 74% 10%, 100% 100%, 0% 100%)",
            }}
          />
        ))}
      </div>

      <main className="relative z-10 flex min-h-screen items-center justify-center px-4 pb-10 pt-10">
        <section className="glass-login-card w-full max-w-[510px] rounded-[28px] border border-white/20 bg-white/18 p-5 shadow-[0_30px_70px_rgba(5,15,25,0.38)] backdrop-blur-2xl" aria-labelledby="auth-title">
          <div className="px-6 pb-3 pt-2 text-center">
            <h1 id="auth-title" className="font-[Georgia,serif] text-[clamp(3rem,12vw,4.2rem)] font-normal leading-none text-slate-950">
              {mode === "login" ? "Login" : "Register"}
            </h1>
          </div>

          <form className="space-y-5 px-6 pb-4 pt-3" onSubmit={handleSubmit} noValidate>
            {mode === "register" && <Field id="name" label="Full Name" type="text" autoComplete="name" />}
            <Field id="email" label="Email" type="email" autoComplete="email" />
            <PasswordField id="password" label="Password" visible={showPassword} toggle={() => setShowPassword((value) => !value)} autoComplete={mode === "login" ? "current-password" : "new-password"} />
            {mode === "register" && <PasswordField id="confirmPassword" label="Confirm Password" visible={showPassword} toggle={() => setShowPassword((value) => !value)} autoComplete="new-password" />}

            {mode === "register" && <p className="text-sm text-slate-900/80">Use 10+ characters with uppercase, lowercase, and a number.</p>}
            {error && <p role="alert" className="rounded-md border border-red-900/20 bg-red-50/70 px-3 py-2 text-sm font-medium text-red-950">{error}</p>}

            <button
              type="submit"
              disabled={submitting}
              className="mt-3 flex w-full items-center justify-center gap-2 rounded-full bg-[#0b1d2b] px-4 py-4 text-xl font-medium text-white shadow-[0_8px_18px_rgba(11,29,43,0.28)] transition hover:brightness-110 disabled:cursor-not-allowed disabled:opacity-60"
            >
              {submitting && <LoaderCircle className="h-5 w-5 animate-spin" aria-hidden="true" />}
              {submitting ? "Please wait" : mode === "login" ? "Login" : "Create Account"}
            </button>

            <div className="pt-2 text-center text-[1.02rem] text-slate-950">
              {mode === "login" ? "New here? " : "Already have an account? "}
              <Link href={mode === "login" ? "/register" : "/login"} className="font-semibold underline underline-offset-2">
                {mode === "login" ? "Create an account" : "Login"}
              </Link>
            </div>
          </form>
        </section>
      </main>
    </div>
  );
}

function Field({ id, label, type, autoComplete }: { id: string; label: string; type: string; autoComplete: string }) {
  return <div><label htmlFor={id} className="mb-2 block text-base font-medium text-slate-950">{label}</label><input id={id} name={id} type={type} autoComplete={autoComplete} required className="w-full border-0 border-b border-slate-800/60 bg-transparent pb-2 text-lg text-slate-950 outline-none focus:border-slate-950" /></div>;
}

function PasswordField({ id, label, visible, toggle, autoComplete }: { id: string; label: string; visible: boolean; toggle: () => void; autoComplete: string }) {
  return <div><label htmlFor={id} className="mb-2 block text-base font-medium text-slate-950">{label}</label><div className="relative"><input id={id} name={id} type={visible ? "text" : "password"} autoComplete={autoComplete} required className="w-full border-0 border-b border-slate-800/60 bg-transparent pb-2 pr-10 text-lg text-slate-950 outline-none focus:border-slate-950" /><button type="button" onClick={toggle} className="absolute right-0 top-0 p-1 text-slate-900" aria-label={visible ? "Hide password" : "Show password"}>{visible ? <EyeOff className="h-5 w-5" /> : <Eye className="h-5 w-5" />}</button></div></div>;
}
