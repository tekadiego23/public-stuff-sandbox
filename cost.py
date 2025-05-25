import pandas as pd
import numpy as np
import requests
from datetime import timedelta
from google.cloud import bigquery

# === 1. RÉCUPÉRATION DES JOBS VIA API REST ===
REST_API_URL = "https://your.api.endpoint/jobs"
response = requests.get(REST_API_URL)
response.raise_for_status()
df_jobs = pd.DataFrame(response.json())

df_jobs['JobStartTime'] = pd.to_datetime(df_jobs['JobStartTime'])
df_jobs['JobCompletionTime'] = pd.to_datetime(df_jobs['JobCompletionTime'])
df_jobs['cluster_name'] = df_jobs['cluster_name'].astype(str)

# === 2. FILTRER UNIQUEMENT LES ENVIRONNEMENTS "firebird" ===
df_jobs = df_jobs[df_jobs['cluster_name'].str.lower().str.contains("firebird")].copy()

# === 3. CALCUL DES DATES DE JOB & DURÉES ===
df_jobs['start_date'] = df_jobs['JobStartTime'].dt.normalize()
df_jobs['end_date'] = df_jobs['JobCompletionTime'].dt.normalize()
df_jobs['duration'] = (df_jobs['JobCompletionTime'] - df_jobs['JobStartTime']).dt.total_seconds()

# Étendre chaque job sur toutes les dates traversées
date_ranges = df_jobs.apply(
    lambda row: pd.date_range(start=row['start_date'], end=row['end_date'], freq='D'), axis=1
)
df_jobs_expanded = df_jobs.loc[df_jobs.index.repeat(date_ranges.str.len())].copy()
df_jobs_expanded['day'] = np.concatenate(date_ranges.to_numpy())

# Calcul vectorisé des plages horaires journalières
df_jobs_expanded['day_start'] = df_jobs_expanded[['JobStartTime', 'day']].max(axis=1)
df_jobs_expanded['day_end'] = df_jobs_expanded.apply(
    lambda row: min(row['JobCompletionTime'], row['day'] + timedelta(days=1)), axis=1
)
df_jobs_expanded['duration_day'] = (df_jobs_expanded['day_end'] - df_jobs_expanded['day_start']).dt.total_seconds()
df_jobs_expanded['ratio'] = df_jobs_expanded['duration_day'] / df_jobs_expanded['duration']

# === 4. RÉCUPÉRATION DES COÛTS JOURNALIERS GCP (BigQuery) ===
client = bigquery.Client()
dates = df_jobs_expanded['day'].dt.date.unique().tolist()
query = f"""
SELECT DATE(day) AS day, env, SUM(cost) AS cost
FROM `your_project.your_dataset.daily_costs`
WHERE DATE(day) IN UNNEST(@dates)
GROUP BY day, env
"""
job_config = bigquery.QueryJobConfig(
    query_parameters=[bigquery.ArrayQueryParameter("dates", "DATE", dates)]
)
df_costs = client.query(query, job_config=job_config).to_dataframe()
df_costs['day'] = pd.to_datetime(df_costs['day'])

# === 5. JOINDRE AVEC LES COÛTS PAR JOUR ===
df_jobs_expanded = df_jobs_expanded.merge(
    df_costs, left_on=['day', 'cluster_name'], right_on=['day', 'env'], how='left'
)

# === 6. CALCUL DU COÛT GCP PAR JOB ===
df_jobs_expanded['job_cost'] = df_jobs_expanded['ratio'] * df_jobs_expanded['cost']
df_result = df_jobs_expanded.groupby('JobId', as_index=False)['job_cost'].sum()

# === 7. AFFICHAGE OU EXPORT ===
import ace_tools as tools
tools.display_dataframe_to_user(name="Coût GCP par JobId (firebird only)", dataframe=df_result)

# Pour un export optionnel :
# df_result.to_csv("job_costs_firebird.csv", index=False)
