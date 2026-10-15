"use client";

import { useState, type FormEvent } from "react";
import { LoaderCircle, Send } from "lucide-react";
import { api, ApiError, type PatientAssistantResponse } from "@/lib/api";

export default function PatientRecordAssistant() {
  const [question, setQuestion] = useState("");
  const [response, setResponse] = useState<PatientAssistantResponse | null>(null);
  const [error, setError] = useState("");
  const [loading, setLoading] = useState(false);

  async function submit(event: FormEvent<HTMLFormElement>) {
    event.preventDefault();
    setLoading(true);
    setResponse(null);
    setError("");
    try {
      setResponse(await api.askPatientAssistant(question));
    } catch (caught) {
      setError(
        caught instanceof ApiError
          ? caught.message
          : "Unable to verify an answer from your records. Please try again.",
      );
    } finally {
      setLoading(false);
    }
  }

  return (
    <section
      aria-labelledby="patient-record-assistant-title"
      className="risk-panel mx-auto mt-6 w-full max-w-6xl rounded-2xl p-5 sm:p-8"
    >
      <div className="max-w-3xl">
        <p className="text-xs font-semibold uppercase tracking-[0.16em] text-cyan-800">
          Private record lookup
        </p>
        <h2
          id="patient-record-assistant-title"
          className="mt-2 font-[Georgia,serif] text-2xl text-[#0b3447]"
        >
          Ask about your saved records
        </h2>
        <p className="mt-2 text-sm leading-6 text-slate-600">
          Answers use only your saved lung-risk assessment. This app currently
          has no appointment, allergy, medication, diagnosis, or lab-result
          records. This assistant does not diagnose or recommend treatment.
        </p>
        <form onSubmit={submit} className="mt-5">
          <label
            htmlFor="patient-record-question"
            className="block text-sm font-medium text-slate-800"
          >
            Your question
          </label>
          <textarea
            id="patient-record-question"
            value={question}
            onChange={(event) => setQuestion(event.target.value)}
            maxLength={1000}
            required
            rows={3}
            placeholder="For example: What is my recorded smoking status?"
            className="risk-input mt-2 w-full resize-y"
          />
          <div className="mt-3 flex flex-wrap items-center justify-between gap-3">
            <span className="text-xs text-slate-500">
              Supported lookups send your question and relevant records only to
              your local configured Ollama model. Questions are not saved in
              assistant history.
            </span>
            <button
              type="submit"
              disabled={loading || !question.trim()}
              className="inline-flex items-center gap-2 rounded-full bg-[#0b3447] px-5 py-2.5 text-sm font-semibold text-white disabled:cursor-not-allowed disabled:opacity-50"
            >
              {loading ? (
                <LoaderCircle className="h-4 w-4 animate-spin" />
              ) : (
                <Send className="h-4 w-4" />
              )}
              Ask about my records
            </button>
          </div>
        </form>
        {loading && (
          <p role="status" className="mt-4 text-sm text-slate-600">
            Checking your authorized records…
          </p>
        )}
        {error && (
          <p role="alert" className="mt-4 rounded-lg bg-red-50 p-3 text-sm text-red-800">
            {error}
          </p>
        )}
        {response && (
          <div
            aria-live="polite"
            className="mt-5 rounded-xl border border-cyan-200 bg-cyan-50 p-4"
          >
            <p className="text-sm leading-6 text-slate-900">{response.answer}</p>
            {response.sources.length > 0 && (
              <p className="mt-3 text-xs text-slate-600">
                Source: your assessment saved{" "}
                {new Date(response.sources[0].recorded_at).toLocaleString()}.
              </p>
            )}
          </div>
        )}
      </div>
    </section>
  );
}
