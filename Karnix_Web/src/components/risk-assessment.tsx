"use client";

import { useEffect, useMemo, useState, type ChangeEvent } from "react";
import {
  Check,
  ChevronLeft,
  ChevronRight,
  LoaderCircle,
  LogOut,
} from "lucide-react";
import { useRouter } from "next/navigation";
import {
  api,
  ApiError,
  API_URL,
  type DiseaseModelContract,
  type RiskAssessment,
} from "@/lib/api";
import { useAuth } from "@/components/auth-provider";

type FormState = Record<string, unknown>;
const steps = [
  "Basic Profile",
  "Smoking & Tobacco",
  "Lifestyle",
  "Environment",
  "Occupational Exposure",
  "Family History",
  "Review",
];
const initial: FormState = {
  age: 52,
  gender: "male",
  ethnicity: "white",
  smoking_status: "former",
  cigarettes_per_day: 10,
  years_smoked: 20,
  age_started_smoking: 25,
  secondhand_smoke_exposure: "occasional",
  tobacco_type: "cigarettes",
  other_tobacco_type: null,
  alcohol_consumption: "occasional",
  physical_activity: "moderate",
  weekly_physical_activity: "1_3_hours",
  sleep_duration: "6_8_hours",
  diet_quality: "average",
  height_cm: 180,
  weight_kg: 88,
  air_pollution_exposure: "high",
  living_environment: "urban",
  traffic_pollution_exposure: "high",
  residential_pollution_exposure: "moderate",
  occupational_exposure: "yes",
  occupational_exposure_types: ["diesel_exhaust", "chemical_dust"],
  occupational_exposure_duration: "5_10_years",
  family_history_lung_cancer: "no",
  family_history_other_cancer: "yes",
  family_cancer_relationship: null,
};
const options = {
  gender: [
    ["female", "Female"],
    ["male", "Male"],
    ["non_binary", "Non-binary"],
    ["prefer_not_to_say", "Prefer not to say"],
  ],
  ethnicity: [
    ["asian", "Asian"],
    ["black", "Black or African descent"],
    ["hispanic_latino", "Hispanic or Latino"],
    ["white", "White"],
    ["middle_eastern", "Middle Eastern"],
    ["indigenous", "Indigenous"],
    ["other", "Other"],
    ["prefer_not_to_say", "Prefer not to say"],
  ],
  secondhand_smoke_exposure: [
    ["never", "Never"],
    ["occasional", "Occasional"],
    ["frequent", "Frequent"],
  ],
  alcohol_consumption: [
    ["never", "Never"],
    ["occasional", "Occasional"],
    ["moderate", "Moderate"],
    ["frequent", "Frequent"],
  ],
  physical_activity: [
    ["very_low", "Very low"],
    ["low", "Low"],
    ["moderate", "Moderate"],
    ["high", "High"],
  ],
  weekly_physical_activity: [
    ["none", "None"],
    ["under_1_hour", "Less than 1 hour"],
    ["1_3_hours", "1-3 hours"],
    ["3_5_hours", "3-5 hours"],
    ["over_5_hours", "More than 5 hours"],
  ],
  sleep_duration: [
    ["under_5_hours", "Less than 5 hours"],
    ["5_6_hours", "5-6 hours"],
    ["6_8_hours", "6-8 hours"],
    ["over_8_hours", "More than 8 hours"],
  ],
  diet_quality: [
    ["poor", "Poor"],
    ["average", "Average"],
    ["good", "Good"],
  ],
  exposure: [
    ["low", "Low"],
    ["moderate", "Moderate"],
    ["high", "High"],
  ],
};

export default function RiskAssessment() {
  const router = useRouter();
  const { user, loading, logout } = useAuth();
  const [step, setStep] = useState(0);
  const [form, setForm] = useState<FormState>(initial);
  const [saved, setSaved] = useState<RiskAssessment | null>(null);
  const [existing, setExisting] = useState<RiskAssessment[]>([]);
  const [diseaseModels, setDiseaseModels] = useState<DiseaseModelContract[]>(
    [],
  );
  const [demoPredictions, setDemoPredictions] = useState<
    Array<Record<string, unknown>>
  >([]);
  const [error, setError] = useState("");
  const [saving, setSaving] = useState(false);

  useEffect(() => {
    if (!loading && !user) router.replace("/login");
    if (user) {
      api
        .listRiskAssessments()
        .then(setExisting)
        .catch(() => undefined);
      api
        .diseaseCatalog()
        .then(({ diseases }) => setDiseaseModels(diseases))
        .catch(() => undefined);
    }
  }, [loading, router, user]);

  const packYears = useMemo(
    () =>
      Number(
        (
          (Number(form.cigarettes_per_day) * Number(form.years_smoked)) /
          20
        ).toFixed(2),
      ),
    [form],
  );
  const bmi = useMemo(() => {
    const height = Number(form.height_cm) / 100;
    return height > 0
      ? Number((Number(form.weight_kg) / (height * height)).toFixed(2))
      : 0;
  }, [form]);

  function update(key: string, value: unknown) {
    setForm((current) => ({ ...current, [key]: value }));
    setError("");
  }
  function updateNumber(event: ChangeEvent<HTMLInputElement>) {
    update(
      event.target.name,
      event.target.value === "" ? "" : Number(event.target.value),
    );
  }
  function toggleExposure(value: string) {
    const current = form.occupational_exposure_types as string[];
    update(
      "occupational_exposure_types",
      current.includes(value)
        ? current.filter((item) => item !== value)
        : [...current, value],
    );
  }
  function validateStep(targetStep: number) {
    const requiredByStep: Record<number, string[]> = {
      0: ["age", "gender", "ethnicity"],
      1: [
        "smoking_status",
        "secondhand_smoke_exposure",
        "tobacco_type",
        "alcohol_consumption",
      ],
      2: [
        "physical_activity",
        "weekly_physical_activity",
        "sleep_duration",
        "diet_quality",
        "height_cm",
        "weight_kg",
      ],
      3: [
        "air_pollution_exposure",
        "living_environment",
        "traffic_pollution_exposure",
        "residential_pollution_exposure",
      ],
      4: ["occupational_exposure"],
      5: ["family_history_lung_cancer", "family_history_other_cancer"],
    };
    const missing = (requiredByStep[targetStep] ?? []).find(
      (field) =>
        form[field] === "" || form[field] === null || form[field] === undefined,
    );
    if (missing) {
      setError(
        `Please complete ${missing.replaceAll("_", " ")} before continuing.`,
      );
      return false;
    }
    if (
      targetStep === 1 &&
      form.smoking_status !== "never" &&
      (!form.age_started_smoking ||
        !form.cigarettes_per_day ||
        !form.years_smoked)
    ) {
      setError("Please complete the smoking history before continuing.");
      return false;
    }
    if (
      targetStep === 4 &&
      form.occupational_exposure === "yes" &&
      (!(form.occupational_exposure_types as string[]).length ||
        !form.occupational_exposure_duration)
    ) {
      setError(
        "Please complete the occupational exposure details before continuing.",
      );
      return false;
    }
    if (
      targetStep === 5 &&
      form.family_history_lung_cancer === "yes" &&
      !form.family_cancer_relationship
    ) {
      setError("Please select the family relationship before continuing.");
      return false;
    }
    setError("");
    return true;
  }
  function next() {
    if (validateStep(step))
      setStep((current) => Math.min(current + 1, steps.length - 1));
  }
  function back() {
    setStep((current) => Math.max(current - 1, 0));
  }
  async function submit() {
    for (const index of [0, 1, 2, 3, 4, 5]) {
      if (!validateStep(index)) {
        setStep(index);
        return;
      }
    }
    setSaving(true);
    setError("");
    try {
      const submittedForm = { ...form };
      delete submittedForm.pack_years;
      delete submittedForm.bmi;
      const result = await api.createRiskAssessment({
        ...submittedForm,
        age: Number(form.age),
        height_cm: Number(form.height_cm),
        weight_kg: Number(form.weight_kg),
      });
      const demo = await api
        .demoPredictions()
        .catch(() => ({ predictions: [] }));
      setDemoPredictions(demo.predictions);
      setSaved(result.assessment);
      setExisting((current) => [result.assessment, ...current]);
    } catch (caught) {
      setError(
        caught instanceof ApiError
          ? caught.message
          : "Unable to save this assessment. Please try again.",
      );
    } finally {
      setSaving(false);
    }
  }

  if (loading || !user)
    return (
      <main className="flex min-h-screen items-center justify-center bg-[#081a2b]">
        <LoaderCircle className="h-7 w-7 animate-spin text-white" />
      </main>
    );
  if (saved)
    return (
      <Success
        name={user.name}
        assessment={saved}
        diseaseModels={diseaseModels}
        demoPredictions={demoPredictions}
        onLogout={async () => {
          await logout();
          router.replace("/login");
        }}
      />
    );

  return (
    <main className="risk-shell min-h-screen px-4 py-6 text-slate-950 sm:px-8">
      <div className="mx-auto max-w-6xl">
        <header className="mb-8 flex items-center justify-between gap-4 text-white">
          <div>
            <p className="text-xs font-semibold uppercase tracking-[0.18em] text-cyan-200">
              Karnix research platform
            </p>
            <h1 className="mt-2 font-[Georgia,serif] text-3xl sm:text-4xl">
              Multi Model Disease Risk Assessment
            </h1>
          </div>
          <button
            onClick={async () => {
              await logout();
              router.replace("/login");
            }}
            className="inline-flex items-center gap-2 rounded-full border border-white/30 px-4 py-2 text-sm hover:bg-white/10"
          >
            <LogOut className="h-4 w-4" />
            Log out
          </button>
        </header>
        <div className="grid gap-6 lg:grid-cols-[260px_1fr]">
          <aside className="risk-panel h-fit rounded-2xl p-5">
            <p className="text-sm font-semibold text-slate-700">
              Assessment progress
            </p>
            <div className="mt-4 space-y-2">
              {steps.map((label, index) => (
                <button
                  key={label}
                  onClick={() => index <= step && setStep(index)}
                  className={`flex w-full items-center gap-3 rounded-xl p-3 text-left text-sm ${index === step ? "bg-[#0b3447] text-white" : index < step ? "text-[#0b3447]" : "text-slate-500"}`}
                >
                  <span
                    className={`flex h-7 w-7 items-center justify-center rounded-full border text-xs ${index < step ? "border-cyan-700 bg-cyan-100" : "border-current"}`}
                  >
                    {index < step ? <Check className="h-4 w-4" /> : index + 1}
                  </span>
                  {label}
                </button>
              ))}
            </div>
          </aside>
          <section className="risk-panel rounded-2xl p-5 sm:p-8">
            <div className="mb-8">
              <div className="mb-3 flex justify-between text-sm font-medium">
                <span>
                  Step {step + 1} of {steps.length}
                </span>
                <span>{Math.round(((step + 1) / steps.length) * 100)}%</span>
              </div>
              <div className="h-2 overflow-hidden rounded-full bg-slate-200">
                <div
                  className="h-full rounded-full bg-cyan-700 transition-all"
                  style={{ width: `${((step + 1) / steps.length) * 100}%` }}
                />
              </div>
            </div>
            {step === 0 && (
              <Demographics
                form={form}
                update={update}
                updateNumber={updateNumber}
              />
            )}
            {step === 1 && (
              <Smoking
                form={form}
                update={update}
                updateNumber={updateNumber}
                packYears={packYears}
              />
            )}
            {step === 2 && (
              <Lifestyle
                form={form}
                update={update}
                updateNumber={updateNumber}
                bmi={bmi}
              />
            )}
            {step === 3 && <Environment form={form} update={update} />}
            {step === 4 && (
              <Occupational
                form={form}
                update={update}
                toggle={toggleExposure}
              />
            )}
            {step === 5 && <Family form={form} update={update} />}
            {step === 6 && (
              <Review
                form={form}
                packYears={packYears}
                bmi={bmi}
                diseaseModels={diseaseModels}
              />
            )}
            {error && (
              <p
                role="alert"
                className="mt-6 rounded-lg bg-red-50 p-3 text-sm font-medium text-red-800"
              >
                {error}
              </p>
            )}
            <div className="mt-8 flex justify-between gap-3 border-t border-slate-200 pt-5">
              <button
                onClick={back}
                disabled={step === 0}
                className="inline-flex items-center gap-1 rounded-full border border-slate-300 px-5 py-2.5 text-sm disabled:opacity-40"
              >
                <ChevronLeft className="h-4 w-4" />
                Back
              </button>
              {step < 6 ? (
                <button
                  onClick={next}
                  className="inline-flex items-center gap-1 rounded-full bg-[#0b3447] px-6 py-2.5 text-sm font-semibold text-white"
                >
                  Continue
                  <ChevronRight className="h-4 w-4" />
                </button>
              ) : (
                <button
                  onClick={submit}
                  disabled={saving}
                  className="inline-flex items-center gap-2 rounded-full bg-cyan-700 px-6 py-2.5 text-sm font-semibold text-white disabled:opacity-60"
                >
                  {saving && <LoaderCircle className="h-4 w-4 animate-spin" />}
                  Submit assessment
                </button>
              )}
            </div>
          </section>
        </div>
        <p className="mx-auto mt-6 max-w-3xl text-center text-xs leading-5 text-slate-300">
          This assessment estimates risk using selected background and lifestyle
          factors. It is not a diagnosis and does not replace evaluation by a
          qualified healthcare professional.
        </p>
        {existing.length > 0 && (
          <p className="mt-4 text-center text-xs text-slate-300">
            You have {existing.length} saved assessment
            {existing.length === 1 ? "" : "s"}.
          </p>
        )}
      </div>
    </main>
  );
}

function Heading({
  title,
  description,
}: {
  title: string;
  description: string;
}) {
  return (
    <div className="mb-6">
      <h2 className="text-2xl font-semibold text-[#0b3447]">{title}</h2>
      <p className="mt-1 text-sm text-slate-600">{description}</p>
    </div>
  );
}
function SelectField({
  label,
  name,
  value,
  onChange,
  values,
}: {
  label: string;
  name: string;
  value: unknown;
  onChange: (key: string, value: unknown) => void;
  values: readonly (readonly string[])[];
}) {
  return (
    <label className="block text-sm font-medium">
      {label}
      <select
        name={name}
        value={String(value ?? "")}
        onChange={(event) => onChange(name, event.target.value)}
        className="risk-input mt-2 w-full"
      >
        {values.map(([key, text]) => (
          <option key={key} value={key}>
            {text}
          </option>
        ))}
      </select>
    </label>
  );
}
function NumberField({
  label,
  name,
  value,
  onChange,
  min = 0,
}: {
  label: string;
  name: string;
  value: unknown;
  onChange: (event: ChangeEvent<HTMLInputElement>) => void;
  min?: number;
}) {
  return (
    <label className="block text-sm font-medium">
      {label}
      <input
        name={name}
        type="number"
        min={min}
        value={String(value ?? "")}
        onChange={onChange}
        className="risk-input mt-2 w-full"
      />
    </label>
  );
}
function Grid({ children }: { children: React.ReactNode }) {
  return <div className="grid gap-5 sm:grid-cols-2">{children}</div>;
}
function Demographics({
  form,
  update,
  updateNumber,
}: {
  form: FormState;
  update: (key: string, value: unknown) => void;
  updateNumber: (event: ChangeEvent<HTMLInputElement>) => void;
}) {
  return (
    <>
      <Heading
        title="Basic profile"
        description="Tell us about the background factors used for this research assessment."
      />
      <Grid>
        <NumberField
          label="Age"
          name="age"
          value={form.age}
          onChange={updateNumber}
          min={18}
        />
        <SelectField
          label="Gender"
          name="gender"
          value={form.gender}
          onChange={update}
          values={options.gender}
        />
        <SelectField
          label="Ethnicity"
          name="ethnicity"
          value={form.ethnicity}
          onChange={update}
          values={options.ethnicity}
        />
      </Grid>
    </>
  );
}
function Smoking({
  form,
  update,
  updateNumber,
  packYears,
}: {
  form: FormState;
  update: (key: string, value: unknown) => void;
  updateNumber: (event: ChangeEvent<HTMLInputElement>) => void;
  packYears: number;
}) {
  const smoker = form.smoking_status !== "never";
  return (
    <>
      <Heading
        title="Smoking & tobacco"
        description="Smoking history is one of several background risk factors. Pack-years are calculated automatically."
      />
      <Grid>
        <SelectField
          label="Smoking status"
          name="smoking_status"
          value={form.smoking_status}
          onChange={update}
          values={[
            ["never", "Never smoker"],
            ["former", "Former smoker"],
            ["current", "Current smoker"],
          ]}
        />
        <SelectField
          label="Secondhand smoke exposure"
          name="secondhand_smoke_exposure"
          value={form.secondhand_smoke_exposure}
          onChange={update}
          values={options.secondhand_smoke_exposure}
        />
        <SelectField
          label="Tobacco products"
          name="tobacco_type"
          value={form.tobacco_type}
          onChange={update}
          values={[
            ["none", "None"],
            ["cigarettes", "Cigarettes"],
            ["cigars", "Cigars"],
            ["pipe", "Pipe"],
            ["smokeless", "Smokeless tobacco"],
            ["other", "Other"],
          ]}
        />
        {form.tobacco_type === "other" && (
          <label className="text-sm font-medium">
            Other tobacco type
            <input
              value={String(form.other_tobacco_type ?? "")}
              onChange={(event) =>
                update("other_tobacco_type", event.target.value)
              }
              className="risk-input mt-2 w-full"
            />
          </label>
        )}
        {smoker && (
          <>
            <NumberField
              label="Cigarettes per day"
              name="cigarettes_per_day"
              value={form.cigarettes_per_day}
              onChange={updateNumber}
            />
            <NumberField
              label="Years smoked"
              name="years_smoked"
              value={form.years_smoked}
              onChange={updateNumber}
            />
            <NumberField
              label="Age started smoking"
              name="age_started_smoking"
              value={form.age_started_smoking}
              onChange={updateNumber}
            />
            <div className="rounded-xl bg-cyan-50 p-4 text-sm">
              <strong>Calculated pack-years: {packYears}</strong>
              <p className="mt-1 text-slate-600">
                (cigarettes per day × years smoked) ÷ 20
              </p>
            </div>
          </>
        )}
        <SelectField
          label="Alcohol consumption"
          name="alcohol_consumption"
          value={form.alcohol_consumption}
          onChange={update}
          values={options.alcohol_consumption}
        />
      </Grid>
    </>
  );
}
function Lifestyle({
  form,
  update,
  updateNumber,
  bmi,
}: {
  form: FormState;
  update: (key: string, value: unknown) => void;
  updateNumber: (event: ChangeEvent<HTMLInputElement>) => void;
  bmi: number;
}) {
  return (
    <>
      <Heading
        title="Lifestyle"
        description="These non-clinical factors help create a consistent research profile."
      />
      <Grid>
        <SelectField
          label="Physical activity"
          name="physical_activity"
          value={form.physical_activity}
          onChange={update}
          values={options.physical_activity}
        />
        <SelectField
          label="Weekly activity"
          name="weekly_physical_activity"
          value={form.weekly_physical_activity}
          onChange={update}
          values={options.weekly_physical_activity}
        />
        <SelectField
          label="Sleep duration"
          name="sleep_duration"
          value={form.sleep_duration}
          onChange={update}
          values={options.sleep_duration}
        />
        <SelectField
          label="Diet quality"
          name="diet_quality"
          value={form.diet_quality}
          onChange={update}
          values={options.diet_quality}
        />
        <NumberField
          label="Height (cm)"
          name="height_cm"
          value={form.height_cm}
          onChange={updateNumber}
          min={80}
        />
        <NumberField
          label="Weight (kg)"
          name="weight_kg"
          value={form.weight_kg}
          onChange={updateNumber}
          min={20}
        />
        <div className="rounded-xl bg-cyan-50 p-4 text-sm sm:col-span-2">
          <strong>Calculated BMI: {bmi || "-"}</strong>
          <p className="mt-1 text-slate-600">
            BMI is stored as a background feature, not a diagnosis.
          </p>
        </div>
      </Grid>
    </>
  );
}
function Environment({
  form,
  update,
}: {
  form: FormState;
  update: (key: string, value: unknown) => void;
}) {
  return (
    <>
      <Heading
        title="Environment"
        description="Describe the general exposure levels around your living environment."
      />
      <Grid>
        <SelectField
          label="Air pollution exposure"
          name="air_pollution_exposure"
          value={form.air_pollution_exposure}
          onChange={update}
          values={options.exposure}
        />
        <SelectField
          label="Living environment"
          name="living_environment"
          value={form.living_environment}
          onChange={update}
          values={[
            ["urban", "Urban"],
            ["suburban", "Suburban"],
            ["rural", "Rural"],
          ]}
        />
        <SelectField
          label="Traffic pollution exposure"
          name="traffic_pollution_exposure"
          value={form.traffic_pollution_exposure}
          onChange={update}
          values={options.exposure}
        />
        <SelectField
          label="Residential smoke/pollution"
          name="residential_pollution_exposure"
          value={form.residential_pollution_exposure}
          onChange={update}
          values={options.exposure}
        />
      </Grid>
    </>
  );
}
function Occupational({
  form,
  update,
  toggle,
}: {
  form: FormState;
  update: (key: string, value: unknown) => void;
  toggle: (value: string) => void;
}) {
  const types = [
    "radon",
    "asbestos",
    "diesel_exhaust",
    "chemical_dust",
    "industrial_fumes",
    "smoke",
    "other",
  ];
  return (
    <>
      <Heading
        title="Occupational exposure"
        description="No employer details are collected. Select unknown if you are unsure."
      />
      <div className="max-w-md">
        <SelectField
          label="Exposure to substances that may affect lung health?"
          name="occupational_exposure"
          value={form.occupational_exposure}
          onChange={update}
          values={[
            ["no", "No"],
            ["yes", "Yes"],
            ["unknown", "Unknown"],
          ]}
        />
      </div>
      {form.occupational_exposure === "yes" && (
        <div className="mt-6 space-y-5">
          <fieldset>
            <legend className="text-sm font-medium">Exposure types</legend>
            <div className="mt-3 grid gap-3 sm:grid-cols-2">
              {types.map((type) => (
                <label key={type} className="flex items-center gap-2 text-sm">
                  <input
                    type="checkbox"
                    checked={(
                      form.occupational_exposure_types as string[]
                    ).includes(type)}
                    onChange={() => toggle(type)}
                  />
                  {type.replaceAll("_", " ")}
                </label>
              ))}
            </div>
          </fieldset>
          <SelectField
            label="Duration of exposure"
            name="occupational_exposure_duration"
            value={form.occupational_exposure_duration ?? ""}
            onChange={update}
            values={[
              ["", "Select duration"],
              ["under_1_year", "Less than 1 year"],
              ["1_5_years", "1-5 years"],
              ["5_10_years", "5-10 years"],
              ["over_10_years", "More than 10 years"],
            ]}
          />
        </div>
      )}
    </>
  );
}
function Family({
  form,
  update,
}: {
  form: FormState;
  update: (key: string, value: unknown) => void;
}) {
  const values = [
    ["yes", "Yes"],
    ["no", "No"],
    ["unknown", "Unknown"],
  ];
  return (
    <>
      <Heading
        title="Family history"
        description="Only broad family-history categories are collected."
      />
      <Grid>
        <SelectField
          label="Family history of lung cancer"
          name="family_history_lung_cancer"
          value={form.family_history_lung_cancer}
          onChange={update}
          values={values}
        />
        <SelectField
          label="Family history of other cancers"
          name="family_history_other_cancer"
          value={form.family_history_other_cancer}
          onChange={update}
          values={values}
        />
        {form.family_history_lung_cancer === "yes" && (
          <SelectField
            label="Relationship"
            name="family_cancer_relationship"
            value={form.family_cancer_relationship ?? ""}
            onChange={update}
            values={[
              ["", "Select relationship"],
              ["parent", "Parent"],
              ["sibling", "Sibling"],
              ["child", "Child"],
              ["grandparent", "Grandparent"],
              ["other_close_relative", "Other close relative"],
              ["unknown", "Unknown"],
            ]}
          />
        )}
      </Grid>
    </>
  );
}
function Review({
  form,
  packYears,
  bmi,
  diseaseModels,
}: {
  form: FormState;
  packYears: number;
  bmi: number;
  diseaseModels: DiseaseModelContract[];
}) {
  const labels = Object.entries(form).filter(
    ([key]) =>
      ![
        "occupational_exposure_types",
        "other_tobacco_type",
        "family_cancer_relationship",
      ].includes(key),
  );
  return (
    <>
      <Heading
        title="Review & submit"
        description="Please verify that the information provided is accurate before submitting."
      />
      <div className="grid gap-3 sm:grid-cols-2">
        {labels.map(([key, value]) => (
          <div key={key} className="rounded-lg bg-slate-50 p-3 text-sm">
            <span className="block text-xs uppercase text-slate-500">
              {key.replaceAll("_", " ")}
            </span>
            <strong>{String(value || "Not provided")}</strong>
          </div>
        ))}
        <div className="rounded-lg bg-cyan-50 p-3 text-sm">
          <span className="block text-xs uppercase text-slate-500">
            Pack-years
          </span>
          <strong>{packYears}</strong>
        </div>
        <div className="rounded-lg bg-cyan-50 p-3 text-sm">
          <span className="block text-xs uppercase text-slate-500">BMI</span>
          <strong>{bmi}</strong>
        </div>
      </div>
      <div className="mt-7">
        <h3 className="text-lg font-semibold text-[#0b3447]">
          Disease model center
        </h3>
        <p className="mt-1 text-sm text-slate-600">
          Research model availability is shown below. Predictions are not
          diagnoses.
        </p>
        <div className="mt-3 grid gap-3 sm:grid-cols-2">
          {diseaseModels.map((model) => (
            <div
              key={model.id}
              className="rounded-xl border border-slate-200 bg-white p-3"
            >
              <div className="flex items-center justify-between gap-2">
                <strong className="text-sm">{model.label}</strong>
                <span
                  className={`rounded-full px-2 py-1 text-[10px] font-semibold ${model.status === "adapter_ready" ? "bg-emerald-100 text-emerald-800" : "bg-slate-100 text-slate-600"}`}
                >
                  {model.status === "adapter_ready" ? "Ready" : "Unavailable"}
                </span>
              </div>
              <p className="mt-1 text-xs text-slate-500">
                {model.modality} · {model.input}
              </p>
            </div>
          ))}
        </div>
      </div>
    </>
  );
}
function Success({
  name,
  assessment,
  diseaseModels,
  demoPredictions,
  onLogout,
}: {
  name: string;
  assessment: RiskAssessment;
  diseaseModels: DiseaseModelContract[];
  demoPredictions: Array<Record<string, unknown>>;
  onLogout: () => Promise<void>;
}) {
  const predictionFor = (id: string) =>
    demoPredictions.find((prediction) => prediction.disease === id);
  return (
    <main className="risk-shell flex min-h-screen items-center justify-center px-4 py-8">
      <section className="risk-panel w-full max-w-2xl rounded-2xl p-8 text-center">
        <div className="mx-auto flex h-14 w-14 items-center justify-center rounded-full bg-cyan-100 text-cyan-800">
          <Check />
        </div>
        <h1 className="mt-5 font-[Georgia,serif] text-3xl text-[#0b3447]">
          Risk profile saved
        </h1>
        <p className="mt-3 text-slate-600">
          Thank you, {name}. Your background information was securely saved for
          future research processing.
        </p>
        <div className="mt-7 rounded-xl border border-slate-200 bg-white p-5 text-left">
          <h2 className="text-lg font-semibold text-[#0b3447]">
            Prediction output
          </h2>
          <p className="mt-1 text-sm text-slate-600">
            Demo predictions use fixed repository sample images and trained
            research adapters. They are not predictions for this user.
          </p>
          <div className="mt-4 grid gap-3 sm:grid-cols-2">
            {diseaseModels.map((model) => {
              const prediction = predictionFor(model.id);
              return (
                <div key={model.id} className="rounded-lg border border-slate-200 bg-white p-3 text-slate-950 shadow-sm">
                  {typeof prediction?.image_url === "string" && (
                    <img
                      src={`${API_URL}${String(prediction.image_url)}`}
                      alt={`${model.label} demo sample`}
                      className="mb-3 h-28 w-full rounded-md bg-slate-900 object-cover"
                    />
                  )}
                  <div className="flex items-center justify-between gap-2">
                    <strong className="text-sm text-slate-950">{model.label}</strong>
                    <span
                      className={`rounded-full px-2 py-1 text-[10px] font-semibold ${prediction?.status === "demo_prediction" ? "bg-emerald-100 text-emerald-800" : "bg-slate-100 text-slate-600"}`}
                    >
                      {prediction?.status === "demo_prediction"
                        ? "Demo output"
                        : "Unavailable"}
                    </span>
                  </div>
                  <p className="mt-2 text-sm font-semibold text-slate-900">
                    {prediction?.status === "demo_prediction"
                      ? `${String(prediction.class).replaceAll("_", " ")} · ${(Number(prediction.probability) * 100).toFixed(1)}% probability`
                      : model.status}
                  </p>
                  {prediction?.status === "demo_prediction" && (
                    <div className="mt-2 h-2 overflow-hidden rounded-full bg-slate-200" aria-label={`${(Number(prediction.probability) * 100).toFixed(1)} percent probability`}>
                      <div className="h-full rounded-full bg-cyan-700" style={{ width: `${Math.max(0, Math.min(100, Number(prediction.probability) * 100))}%` }} />
                    </div>
                  )}
                </div>
              );
            })}
          </div>
        </div>
        <p className="mt-5 text-sm text-slate-600">
          Assessment ID: <span className="font-mono">{assessment.id}</span>
        </p>
        <p className="mt-2 text-xs text-slate-500">
          Research estimate only. Not a diagnosis or medical advice.
        </p>
        <button
          onClick={onLogout}
          className="mt-7 inline-flex items-center gap-2 rounded-full bg-[#0b3447] px-6 py-3 font-semibold text-white"
        >
          <LogOut className="h-4 w-4" />
          Log out
        </button>
      </section>
    </main>
  );
}
