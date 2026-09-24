// Centralized site configuration — edit copy, links and data here.

export const siteConfig = {
  name: "Finaro",
  tagline: "The new standard for modern banking",
  description:
    "Send, save, and invest with one powerful app. Join millions managing their money better with instant transfers, smart budgeting, and zero foreign exchange fees.",
  url: "https://finaro.example.com",
};

export const navLinks = [
  {
    label: "Personal",
    items: [
      { label: "Spending account", description: "Everyday banking, zero fees" },
      { label: "Savings", description: "Earn up to 4.5% APY" },
      { label: "Investing", description: "Grow your portfolio automatically" },
    ],
  },
  {
    label: "Business",
    items: [
      { label: "Business accounts", description: "Built for growing teams" },
      { label: "Payroll", description: "Pay anyone, anywhere" },
      { label: "Developer API", description: "Ship financial products fast" },
    ],
  },
  { label: "Pricing", href: "#pricing" },
  {
    label: "Company",
    items: [
      { label: "About", description: "Our mission and story" },
      { label: "Careers", description: "Join the team" },
      { label: "Blog", description: "News and insights" },
    ],
  },
];

export const heroBadge = "New: Instant global transfers";

export const heroStats = [
  { label: "Speed", finaro: "Instant", bank: "3-5 days", wire: "1-2 days" },
];

export const transferComparison = {
  rows: [
    { name: "finaro", speed: "Instant", fees: "$0", highlight: true },
    { name: "Bank", speed: "3-5 days", fees: "$25-50", highlight: false },
    { name: "Wire", speed: "1-2 days", fees: "$15-35", highlight: false },
  ],
};

export const logos = [
  "Corp",
  "Alt+Shift",
  "GlobalBank",
  "Boltshift",
  "Capsule",
  "Catalog",
];

export const features = [
  {
    title: "Send money globally. Instantly.",
    description:
      "Transfer funds to 160+ countries with the real exchange rate. No hidden fees, no waiting — your money arrives in seconds.",
    href: "#",
  },
  {
    title: "Earn more on your savings.",
    description:
      "Get up to 4.5% APY on your balance with no lock-in period. Your money works harder while staying instantly accessible.",
    href: "#",
    apy: "4.50%",
    apyLabel: "vs avg savings 0.5%",
    chart: [30, 38, 34, 44, 50, 58, 62, 70, 76, 84, 90, 98],
  },
  {
    title: "Build with our developer API.",
    description:
      "Access real-time payment data and automate your finances with our REST API. Full documentation and SDKs included.",
    href: "#",
    code: [
      "--data '{",
      "  'account-balance'",
      "  'portfolio-value'",
      "  'transaction-history'",
      "  'pending-transfers'",
      "  'card-details'",
      "  'exchange-rates'",
      "}'",
    ],
  },
];

export const principles = [
  {
    title: "Instant Card Controls",
    description: "Freeze, limit, or lock your card in one tap.",
    icon: "card",
  },
  {
    title: "Real-time Analytics",
    description: "Track every dollar the moment it moves.",
    icon: "chart",
  },
  {
    title: "Bank-grade Encryption",
    description: "256-bit encryption protects every transaction.",
    icon: "shield",
  },
  {
    title: "24/7 Fraud Monitoring",
    description: "AI-powered systems watch your account around the clock.",
    icon: "eye",
  },
];

export const stats = [
  { value: 5, suffix: "B+", prefix: "$", label: "Processed annually" },
  { value: 99.9, suffix: "%", label: "Uptime guarantee" },
  { value: 150, suffix: "+", label: "Countries supported" },
  { value: 2, suffix: "M+", label: "Active users" },
];

export const testimonials = [
  {
    name: "Amara Okafor",
    role: "CFO, Boltshift",
    quote:
      "Finaro cut our international payout time from days to seconds. It's the financial infrastructure we didn't know we needed.",
  },
  {
    name: "Daniel Reyes",
    role: "Founder, Capsule",
    quote:
      "The developer API is the cleanest we've integrated. We shipped automated payouts in an afternoon.",
  },
  {
    name: "Priya Nair",
    role: "Head of Finance, Catalog",
    quote:
      "Real-time analytics changed how our whole team thinks about cash flow. It's like having a CFO dashboard built in.",
  },
  {
    name: "Marcus Lindqvist",
    role: "COO, GlobalBank Ventures",
    quote:
      "Security and speed usually trade off. Finaro somehow gives us both, at scale.",
  },
  {
    name: "Elena Voss",
    role: "Treasury Lead, Alt+Shift",
    quote:
      "Switching to Finaro saved us over $40,000 a year in wire fees alone.",
  },
];

export const pricingTiers = [
  {
    name: "Starter",
    price: 0,
    period: "/mo",
    description: "For individuals getting started with smarter banking.",
    features: [
      "Free spending account",
      "Instant transfers up to $1,000/mo",
      "Standard card controls",
      "Email support",
    ],
    cta: "Get started",
    highlighted: false,
  },
  {
    name: "Growth",
    price: 19,
    period: "/mo",
    description: "For professionals and growing side businesses.",
    features: [
      "Everything in Starter",
      "Unlimited instant transfers",
      "4.50% APY on savings",
      "Real-time analytics dashboard",
      "Priority support",
    ],
    cta: "Start free trial",
    highlighted: true,
  },
  {
    name: "Business",
    price: 49,
    period: "/mo",
    description: "For teams that need payroll, API access, and controls.",
    features: [
      "Everything in Growth",
      "Full developer API access",
      "Payroll for up to 50 people",
      "Dedicated account manager",
      "Custom compliance reporting",
    ],
    cta: "Talk to sales",
    highlighted: false,
  },
];

export const faqs = [
  {
    question: "How fast are international transfers?",
    answer:
      "Most transfers to Finaro accounts arrive instantly. Transfers to external banks typically settle within seconds to a few minutes, depending on the destination network.",
  },
  {
    question: "Is my money insured?",
    answer:
      "Yes. Funds held with Finaro are protected through partner bank insurance up to the standard coverage limit in your region.",
  },
  {
    question: "Are there any hidden fees?",
    answer:
      "No. Finaro uses the real exchange rate with transparent, upfront pricing. What you see at checkout is exactly what you pay.",
  },
  {
    question: "Can I use Finaro for my business?",
    answer:
      "Absolutely. Our Business plan includes payroll, multi-user access, developer API keys, and dedicated support for growing teams.",
  },
  {
    question: "How do I close my account?",
    answer:
      "You can close your account anytime from Settings, or contact support and we'll help you migrate your data and remaining balance.",
  },
];

export const blogPosts = [
  {
    title: "How real-time payments are reshaping global commerce",
    category: "Industry",
    date: "Aug 12, 2026",
    image: "gradient-1",
  },
  {
    title: "Inside Finaro's fraud detection engine",
    category: "Engineering",
    date: "Jul 29, 2026",
    image: "gradient-2",
  },
  {
    title: "A founder's guide to multi-currency treasury",
    category: "Business",
    date: "Jul 04, 2026",
    image: "gradient-3",
  },
];

export const footerLinks = {
  Product: ["Personal", "Business", "Pricing", "Security"],
  Company: ["About", "Careers", "Blog", "Press"],
  Resources: ["Developer API", "Help center", "Status", "Guides"],
  Legal: ["Privacy", "Terms", "Compliance"],
};
